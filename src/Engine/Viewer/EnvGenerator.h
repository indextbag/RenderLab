#pragma once

#include <Basic/Visitor.h>
#include <OpenGL/FBO.h>
#include <OpenGL/Texture.h>
#include <OpenGL/VAO.h>
#include <OpenGL/Shader.h>

#include <UGM/transform.h>

namespace CppUtil {
	namespace Basic {
		class Image;
	}

	namespace QT {
		class RawAPI_OGLW;
	}

	namespace Engine {
		class Scene;
		class SObj;

		class Sphere;
		class Plane;
		class TriMesh;

		class InfiniteAreaLight;

		// Cubemap Generator, GPU-driven
		class EnvGenerator : public Basic::Visitor {
		public:
			EnvGenerator(QT::RawAPI_OGLW * pOGLW);

		public:
			static const Basic::Ptr<EnvGenerator> New(QT::RawAPI_OGLW * pOGLW) {
				return Basic::New<EnvGenerator>(pOGLW);
			}

		protected:
			virtual ~EnvGenerator() = default;

		public:
			void Init();
			const OpenGL::Texture GetSkybox(Basic::PtrC<Basic::Image> img) const;
			const OpenGL::Texture GetIrradianceMap(Basic::PtrC<Basic::Image> img) const;
			const OpenGL::Texture GetPrefilterMap(Basic::PtrC<Basic::Image> img) const;
			const OpenGL::Texture GetBRDF_LUT() const { return brdfFBO.GetColorTexture(0); }

		private:
			void Visit(Basic::Ptr<Scene> scene);

		private:
			void InitFBOs();

			void InitShaders();
			void InitShader_genSkybox();
			void InitShader_genIrradiance();
			void InitShader_Prefilter();
			void InitShader_brdf();

			void InitBRDF_LUT();
			void UpdateTextures(Basic::PtrC<Basic::Image> img);
			void UpdateSkybox();
			void UpdateIrradianceMap();
			void UpdatePrefilterMap();

			void Clear();

		private:
			QT::RawAPI_OGLW * pOGLW;

			int skyboxSize;
			int irradianceSize;
			int prefilterSize;
			int brdfSize;

			OpenGL::Shader shader_genSkybox;
			OpenGL::Shader shader_genIrradiance;
			OpenGL::Shader shader_prefilter;
			OpenGL::Shader shader_brdf;

			OpenGL::FBO genSkyboxFBO;
			OpenGL::FBO genIrradianceFBO;
			static constexpr int maxMipLevels = 5;
			OpenGL::FBO prefilterFBOs[maxMipLevels];
			bool isInitBrdfFBO{ false };
			OpenGL::FBO brdfFBO;

			Basic::WPtrC<Basic::Image> curImg;
			OpenGL::Texture skybox;
			OpenGL::Texture irradianceMap;
			OpenGL::Texture prefilterMap;

			static const Ubpa::transformf captureViews[6];
			static const OpenGL::FBO::TexTarget mapper[6];
		};
	}
}
