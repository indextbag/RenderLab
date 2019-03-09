#include <CppUtil/Engine/PathTracer.h>

#include <CppUtil/Engine/BVHAccel.h>

#include <CppUtil/Engine/RayIntersector.h>
#include <CppUtil/Engine/VisibilityChecker.h>

#include <CppUtil/Engine/Scene.h>
#include <CppUtil/Engine/SObj.h>
#include <CppUtil/Engine/Ray.h>

#include <CppUtil/Engine/Material.h>
#include <CppUtil/Engine/Light.h>
#include <CppUtil/Engine/LightBase.h>
#include <CppUtil/Engine/BSDF.h>

#include <CppUtil/Basic/Math.h>

#include <glm/geometric.hpp>

using namespace CppUtil::Engine;
using namespace CppUtil::Basic;
using namespace glm;
using namespace std;

PathTracer::PathTracer(Scene::Ptr scene)
	: RayTracer(scene), sampleNumForAreaLight(2), maxDepth(20), bvhAccel(ToPtr(new BVHAccel)) { }

void PathTracer::Init() {
	lights.clear();
	worldToLightVec.clear();
	dir_lightToWorldVec.clear();
	dir_worldToLightVec.clear();
	lightToIdx.clear();

	auto lightComponents = scene->GetLights();
	for (int i = 0; i < lightComponents.size(); i++) {
		auto lightComponent = lightComponents[i];
		auto light = lightComponent->GetLight();

		lightToIdx[light] = i;

		lights.push_back(light);

		mat4 lightToWorld = lightComponent->GetLightToWorldMatrixWithoutScale();
		mat4 worldToLight = inverse(lightToWorld);

		worldToLightVec.push_back(worldToLight);
		dir_lightToWorldVec.push_back(lightToWorld);
		dir_worldToLightVec.push_back(worldToLight);
	}

	bvhAccel->Init(scene->GetRoot());
}

vec3 PathTracer::Trace(Ray::Ptr ray, int depth) {
	auto rayIntersector = ToPtr(new RayIntersector(ray));
	bvhAccel->Accept(rayIntersector);
	auto & closestRst = rayIntersector->GetRst();
	if (!closestRst.closestSObj) {
		//return vec3(0);
		
		float t = 0.5f * (normalize(ray->GetDir()).y + 1.0f);
		vec3 white(1.0f, 1.0f, 1.0f);
		vec3 blue(0.5f, 0.7f, 1.0f);
		return t * white + (1 - t)*blue;
	}
	// 计算碰撞点在灯光空间的位置
	const vec3 hitPos = ray->At(ray->GetTMax());

	vector<vec3> posInLightSpaceVec;

	vec4 hitPos4 = vec4(hitPos, 1);
	size_t lightNum = lights.size();
	for (size_t i = 0; i < lightNum; i++)
		posInLightSpaceVec.push_back(worldToLightVec[i] * hitPos4);

	vec3 emitL(0);
	auto lightComponent = closestRst.closestSObj->GetComponent<Light>();
	if (lightComponent) {
		int idx = lightToIdx[lightComponent->GetLight()];
		emitL = lightComponent->GetLight()->GetMaxL(worldToLightVec[idx] * vec4(ray->GetOrigin(), 1));
	}

	auto material = closestRst.closestSObj->GetComponent<Material>();
	if (material == nullptr)
		return emitL;

	auto bsdf = BSDF::Ptr::Cast(material->GetMat());
	if (bsdf == nullptr)
		return emitL;

	emitL += bsdf->GetEmission();

	bsdf->ChangeNormal(closestRst.texcoord, closestRst.tangent, closestRst.n);

	auto const surfaceToWorld = Math::GenCoordSpace(closestRst.n);
	auto const worldToSurface = transpose(surfaceToWorld);

	// w_out 处于表面坐标系，向外
	auto w_out = normalize(worldToSurface * (-ray->GetDir()));

	vec3 sumLightL(0);
	
	if (!bsdf->IsDelta()) {
		const vec3 shadowOrigin = hitPos;

		for (int i = 0; i < lightNum; i++) {
			auto const light = lights[i];

			const int sampleNum = light->IsDelta() ? 1 : sampleNumForAreaLight;

			for (int j = 0; j < sampleNum; j++) {
				float dist_ToLight;
				float PD;// 概率密度
				vec3 dir_ToLight;
				// dir_ToLight 是单位向量
				const vec3 lightL = light->Sample_L(posInLightSpaceVec[i], dir_ToLight, dist_ToLight, PD);

				if (PD <= 0)
					continue;

				const vec3 dirInWorld = normalize(dir_lightToWorldVec[i] * dir_ToLight);

				// shadowRay 处于世界坐标
				Ray::Ptr shadowRay = ToPtr(new Ray(shadowOrigin, dirInWorld));
				auto checker = ToPtr(new VisibilityChecker(shadowRay, dist_ToLight - 0.001f));
				bvhAccel->Accept(checker);
				auto shadowRst = checker->GetRst();
				if (shadowRst.IsIntersect())
					continue;

				// w_in 处于表面坐标系，应该是单位向量
				const vec3 w_in = normalize(worldToSurface * dirInWorld);

				// 多重重要性采样 Multiple Importance Sampling (MIS)
				float sumPD = sampleNum * PD;
				if (!light->IsDelta()) {
					sumPD += bsdf->PDF(w_out, w_in, closestRst.texcoord);
					//sumPD += bsdf->PDF(w_out, w_in, closestRst.texcoord);
					for (int k = 0; k < lightNum; k++) {
						if (k != i && !lights[k]->IsDelta()) {
							vec3 dirInLight = dir_worldToLightVec[k] * dirInWorld;
							sumPD += sampleNumForAreaLight * lights[k]->PDF(posInLightSpaceVec[i], dirInLight);
						}
					}
				}

				// 在碰撞表面的坐标系计算 dot(n, w_in) 很简单，因为 n = (0, 0, 1)
				const float cos_theta = max(w_in.z, 0.f);

				// evaluate surface bsdf
				const vec3 f = bsdf->F(w_out, w_in, closestRst.texcoord);

				auto weight = (cos_theta / sumPD) * f;
				sumLightL += weight * lightL;
			}
		}
	}
	// 超过深度直接丢弃
	if (depth + 1 >= maxDepth)
		return emitL + sumLightL;

	// 采样 BSDF
	vec3 mat_w_in;
	float matPD;
	const vec3 matF = bsdf->Sample_f(w_out, closestRst.texcoord, mat_w_in, matPD);
	const vec3 matRayDirInWorld = surfaceToWorld * mat_w_in;
	const float abs_cosTheta = abs(mat_w_in.z);

	if (matPD <= 0)
		return emitL + sumLightL;

	// 一定概率丢弃
	//float terminateProbability = 0.f;
	// 这里一定要是 illumination * cos(theta)
	//if (!bsdf->IsDelta() && Math::Illum(matF) * abs_cosTheta < 0.0618f)
	//	terminateProbability = 0.8f;

	//if (Math::Rand_F() < terminateProbability)
	//	return emitL + sumLightL;

	// 重要性采样
	float sumPD = matPD;
	if (!bsdf->IsDelta()) {
		for (int i = 0; i < lightNum; i++) {
			if (lights[i]->IsDelta())
				continue;

			vec3 dirInLight = dir_worldToLightVec[i] * matRayDirInWorld;
			sumPD += sampleNumForAreaLight * lights[i]->PDF(posInLightSpaceVec[i], dirInLight);
		}
	}

	Ray::Ptr matRay = ToPtr(new Ray(hitPos, matRayDirInWorld));
	const vec3 matRayColor = Trace(matRay, depth + 1);

	vec3 matL = abs_cosTheta / (sumPD /** (1.f - terminateProbability)*/) * matF * matRayColor;

	return emitL + sumLightL + matL;
}
