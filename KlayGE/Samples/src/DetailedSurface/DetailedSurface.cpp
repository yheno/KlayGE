#include <KlayGE/KlayGE.hpp>
#include <KFL/ThrowErr.hpp>
#include <KFL/Util.hpp>
#include <KFL/Math.hpp>
#include <KlayGE/Font.hpp>
#include <KlayGE/Renderable.hpp>
#include <KlayGE/RenderableHelper.hpp>
#include <KlayGE/RenderEngine.hpp>
#include <KlayGE/RenderEffect.hpp>
#include <KlayGE/FrameBuffer.hpp>
#include <KlayGE/SceneManager.hpp>
#include <KlayGE/Context.hpp>
#include <KlayGE/ResLoader.hpp>
#include <KlayGE/RenderSettings.hpp>
#include <KlayGE/Mesh.hpp>
#include <KlayGE/GraphicsBuffer.hpp>
#include <KlayGE/Light.hpp>
#include <KlayGE/SceneObjectHelper.hpp>
#include <KlayGE/JudaTexture.hpp>
#include <KlayGE/Camera.hpp>

#include <KlayGE/RenderFactory.hpp>
#include <KlayGE/InputFactory.hpp>

#include <vector>
#include <sstream>

#include "SampleCommon.hpp"
#include "DetailedSurface.hpp"

using namespace std;
using namespace KlayGE;

namespace
{
	enum
	{
		DT_None,
		DT_Bump,
		DT_Parallax,
		DT_ParallaxOcclusion,
		DT_Tessellation
	};

	class RenderDetailedModel : public RenderModel
	{
	public:
		RenderDetailedModel(std::wstring const & name)
			: RenderModel(name)
		{
		}

		void BuildModelInfo()
		{
			RenderFactory& rf = Context::Instance().RenderFactoryInstance();

			RenderLayoutPtr rl = this->Subrenderable(0)->GetRenderLayout();

			AABBox const & pos_bb = this->PosBound();
			AABBox const & tc_bb = this->TexcoordBound();
			float3 const pos_center = pos_bb.Center();
			float3 const pos_extent = pos_bb.HalfSize();
			float3 const tc_center = tc_bb.Center();
			float3 const tc_extent = tc_bb.HalfSize();

			std::vector<float3> positions(rl->NumVertices());
			std::vector<float2> texs(rl->NumVertices());
			for (uint32_t i = 0; i < rl->NumVertexStreams(); ++ i)
			{
				GraphicsBufferPtr const & vb = rl->GetVertexStream(i);
				switch (rl->VertexStreamFormat(i)[0].usage)
				{
				case VEU_Position:
					{
						GraphicsBufferPtr vb_cpu = rf.MakeVertexBuffer(BU_Static, EAH_CPU_Read, nullptr);
						vb_cpu->Resize(vb->Size());
						vb->CopyToBuffer(*vb_cpu);

						GraphicsBuffer::Mapper mapper(*vb_cpu, BA_Read_Only);
						int16_t const * p_16 = mapper.Pointer<int16_t>();
						for (uint32_t j = 0; j < rl->NumVertices(); ++ j)
						{
							positions[j].x() = ((p_16[j * 4 + 0] + 32768) / 65535.0f * 2 - 1) * pos_extent.x() + pos_center.x();
							positions[j].y() = ((p_16[j * 4 + 1] + 32768) / 65535.0f * 2 - 1) * pos_extent.y() + pos_center.y();
							positions[j].z() = ((p_16[j * 4 + 2] + 32768) / 65535.0f * 2 - 1) * pos_extent.z() + pos_center.z();
						}
					}
					break;

				case VEU_TextureCoord:
					if (0 == rl->VertexStreamFormat(i)[0].usage_index)
					{
						GraphicsBufferPtr vb_cpu = rf.MakeVertexBuffer(BU_Static, EAH_CPU_Read, nullptr);
						vb_cpu->Resize(vb->Size());
						vb->CopyToBuffer(*vb_cpu);

						GraphicsBuffer::Mapper mapper(*vb_cpu, BA_Read_Only);
						int16_t const * t_16 = mapper.Pointer<int16_t>();
						for (uint32_t j = 0; j < rl->NumVertices(); ++ j)
						{
							texs[j].x() = ((t_16[j * 2 + 0] + 32768) / 65535.0f * 2 - 1) * tc_extent.x() + tc_center.x();
							texs[j].y() = ((t_16[j * 2 + 1] + 32768) / 65535.0f * 2 - 1) * tc_extent.y() + tc_center.y();
						}
					}
					break;

				default:
					break;
				}
			}

			std::vector<uint32_t> indices(rl->NumIndices());
			{
				GraphicsBufferPtr ib = rl->GetIndexStream();
				GraphicsBufferPtr ib_cpu = rf.MakeVertexBuffer(BU_Static, EAH_CPU_Read, nullptr);
				ib_cpu->Resize(ib->Size());
				ib->CopyToBuffer(*ib_cpu);

				GraphicsBuffer::Mapper mapper(*ib_cpu, BA_Read_Only);
				if (EF_R16UI == rl->IndexStreamFormat())
				{
					uint16_t* p = mapper.Pointer<uint16_t>();
					std::copy(p, p + indices.size(), indices.begin());
				}
				else
				{
					uint32_t* p = mapper.Pointer<uint32_t>();
					std::copy(p, p + indices.size(), indices.begin());
				}
			}

			std::vector<float> distortions(rl->NumVertices(), 0);
			std::vector<uint32_t> vert_times(rl->NumVertices(), 0);
			for (size_t i = 0; i < indices.size(); i += 3)
			{
				uint32_t i0 = indices[i + 0];
				uint32_t i1 = indices[i + 1];
				uint32_t i2 = indices[i + 2];

				float geom_area = MathLib::length(MathLib::cross(positions[i1] - positions[i0], positions[i2] - positions[i0]));
				float tex_area = MathLib::cross(texs[i1] - texs[i0], texs[i2] - texs[i0]);
				float tri_distortion = sqrt(geom_area / tex_area) / 2.5f;
				distortions[i0] += tri_distortion;
				distortions[i1] += tri_distortion;
				distortions[i2] += tri_distortion;
				++ vert_times[i0];
				++ vert_times[i1];
				++ vert_times[i2];
			}
			for (size_t i = 0; i < distortions.size(); ++ i)
			{
				if (vert_times[i] > 0)
				{
					distortions[i] /= vert_times[i];
				}
			}

			ElementInitData init_data;
			init_data.row_pitch = static_cast<uint32_t>(distortions.size() * sizeof(distortions[0]));
			init_data.slice_pitch = 0;
			init_data.data = &distortions[0];
			GraphicsBufferPtr distortion_vb = rf.MakeVertexBuffer(BU_Static, EAH_GPU_Read | EAH_Immutable, &init_data);
			rl->BindVertexStream(distortion_vb, KlayGE::make_tuple(vertex_element(VEU_TextureCoord, 1, EF_R32F)));
		}
	};

	class RenderPolygon : public StaticMesh
	{
	public:
		RenderPolygon(RenderModelPtr const & model, std::wstring const & name)
			: StaticMesh(model, name),
				detail_type_(DT_Parallax), wireframe_(false)
		{
			technique_ = SyncLoadRenderEffect("DetailedSurface.fxml")->TechniqueByName("Parallax");

			tile_bb_[0] = int4(0, 0, 4, 4);
			tile_bb_[1] = int4(4, 0, 4, 4);
			tile_bb_[2] = int4(0, 4, 4, 4);

			*(technique_->Effect().ParameterByName("diffuse_tex_bb")) = tile_bb_[0];
			*(technique_->Effect().ParameterByName("normal_tex_bb")) = tile_bb_[1];
			*(technique_->Effect().ParameterByName("height_tex_bb")) = tile_bb_[2];
			*(technique_->Effect().ParameterByName("tex_size")) = int2(512, 512);
			*(technique_->Effect().ParameterByName("na_length_tex")) = SyncLoadTexture("na_length.dds", EAH_GPU_Read | EAH_Immutable);
		}

		void BuildMeshInfo()
		{
			AABBox const & pos_bb = this->PosBound();
			*(technique_->Effect().ParameterByName("pos_center")) = pos_bb.Center();
			*(technique_->Effect().ParameterByName("pos_extent")) = pos_bb.HalfSize();

			AABBox const & tc_bb = this->TexcoordBound();
			*(technique_->Effect().ParameterByName("tc_center")) = float2(tc_bb.Center().x(), tc_bb.Center().y());
			*(technique_->Effect().ParameterByName("tc_extent")) = float2(tc_bb.HalfSize().x(), tc_bb.HalfSize().y());
		}

		void OnRenderBegin()
		{
			App3DFramework const & app = Context::Instance().AppInstance();
			Camera const & camera = app.ActiveCamera();

			float4x4 const & model = float4x4::Identity();

			*(technique_->Effect().ParameterByName("mvp")) = model * camera.ViewProjMatrix();
			*(technique_->Effect().ParameterByName("world")) = model;
			*(technique_->Effect().ParameterByName("eye_pos")) = camera.EyePos();
		}

		void LightPos(float3 const & light_pos)
		{
			*(technique_->Effect().ParameterByName("light_pos")) = light_pos;
		}

		void LightColor(float3 const & light_color)
		{
			*(technique_->Effect().ParameterByName("light_color")) = light_color;
		}

		void LightFalloff(float3 const & light_falloff)
		{
			*(technique_->Effect().ParameterByName("light_falloff")) = light_falloff;
		}

		void HeightScale(float scale)
		{
			*(technique_->Effect().ParameterByName("height_scale")) = scale;
		}

		void BindJudaTexture(JudaTexturePtr const & juda_tex)
		{
			juda_tex->SetParams(technique_);

			tile_ids_.clear();
			uint32_t level = juda_tex->TreeLevels() - 1;
			for (size_t i = 0; i < sizeof(tile_bb_) / sizeof(tile_bb_[0]); ++ i)
			{
				for (int32_t y = 0; y < tile_bb_[i].w(); ++ y)
				{
					for (int32_t x = 0; x < tile_bb_[i].z(); ++ x)
					{
						tile_ids_.push_back(juda_tex->EncodeTileID(level, tile_bb_[i].x() + x, tile_bb_[i].y() + y));
					}
				}
			}
		}

		std::vector<uint32_t> const & JudaTexTileIDs() const
		{
			return tile_ids_;
		}

		void DetailType(uint32_t dt)
		{
			detail_type_ = dt;
			this->UpdateTech();
		}

		void NaLength(bool len)
		{
			*(technique_->Effect().ParameterByName("use_na_length")) = len;
		}

		void Wireframe(bool wf)
		{
			wireframe_ = wf;
			this->UpdateTech();
		}

	private:
		void UpdateTech()
		{
			std::string tech_name;
			switch (detail_type_)
			{
			case DT_None:
				tech_name = "None";
				break;

			case DT_Bump:
				tech_name = "Bump";
				break;

			case DT_Parallax:
				tech_name = "Parallax";
				break;

			case DT_ParallaxOcclusion:
				tech_name = "ParallaxOcclusion";
				break;

			case DT_Tessellation:
				tech_name = "Tessellation";
				break;

			default:
				tech_name = "None";
				break;
			}

			if (wireframe_)
			{
				tech_name += "Wireframe";
			}

			technique_ = technique_->Effect().TechniqueByName(tech_name);

			if (DT_Tessellation == detail_type_)
			{
				rl_->TopologyType(RenderLayout::TT_3_Ctrl_Pt_PatchList);
			}
			else
			{
				rl_->TopologyType(RenderLayout::TT_TriangleList);
			}
		}

	private:
		int4 tile_bb_[3];
		std::vector<uint32_t> tile_ids_;
		uint32_t detail_type_;
		bool wireframe_;
	};

	class PolygonObject : public SceneObjectHelper
	{
	public:
		PolygonObject()
			: SceneObjectHelper(SOA_Cullable)
		{
			renderable_ = SyncLoadModel("teapot.meshml", EAH_GPU_Read | EAH_Immutable, CreateModelFactory<RenderDetailedModel>(), CreateMeshFactory<RenderPolygon>());
		}

		void LightPos(float3 const & light_pos)
		{
			RenderModelPtr model = checked_pointer_cast<RenderModel>(renderable_);
			for (uint32_t i = 0; i < model->NumSubrenderables(); ++ i)
			{
				checked_pointer_cast<RenderPolygon>(model->Subrenderable(i))->LightPos(light_pos);
			}
		}

		void LightColor(float3 const & light_color)
		{
			RenderModelPtr model = checked_pointer_cast<RenderModel>(renderable_);
			for (uint32_t i = 0; i < model->NumSubrenderables(); ++ i)
			{
				checked_pointer_cast<RenderPolygon>(model->Subrenderable(i))->LightColor(light_color);
			}
		}

		void LightFalloff(float3 const & light_falloff)
		{
			RenderModelPtr model = checked_pointer_cast<RenderModel>(renderable_);
			for (uint32_t i = 0; i < model->NumSubrenderables(); ++ i)
			{
				checked_pointer_cast<RenderPolygon>(model->Subrenderable(i))->LightFalloff(light_falloff);
			}
		}

		void HeightScale(float scale)
		{
			RenderModelPtr model = checked_pointer_cast<RenderModel>(renderable_);
			for (uint32_t i = 0; i < model->NumSubrenderables(); ++ i)
			{
				checked_pointer_cast<RenderPolygon>(model->Subrenderable(i))->HeightScale(scale);
			}
		}

		void BindJudaTexture(JudaTexturePtr const & juda_tex)
		{
			RenderModelPtr model = checked_pointer_cast<RenderModel>(renderable_);
			for (uint32_t i = 0; i < model->NumSubrenderables(); ++ i)
			{
				checked_pointer_cast<RenderPolygon>(model->Subrenderable(i))->BindJudaTexture(juda_tex);
			}
		}

		std::vector<uint32_t> const & JudaTexTileIDs(uint32_t index) const
		{
			RenderModelPtr model = checked_pointer_cast<RenderModel>(renderable_);
			return checked_pointer_cast<RenderPolygon>(model->Subrenderable(index))->JudaTexTileIDs();
		}

		void DetailType(uint32_t dt)
		{
			RenderModelPtr model = checked_pointer_cast<RenderModel>(renderable_);
			for (uint32_t i = 0; i < model->NumSubrenderables(); ++ i)
			{
				checked_pointer_cast<RenderPolygon>(model->Subrenderable(i))->DetailType(dt);
			}
		}

		void NaLength(bool len)
		{
			RenderModelPtr model = checked_pointer_cast<RenderModel>(renderable_);
			for (uint32_t i = 0; i < model->NumSubrenderables(); ++ i)
			{
				checked_pointer_cast<RenderPolygon>(model->Subrenderable(i))->NaLength(len);
			}
		}

		void Wireframe(bool wf)
		{
			RenderModelPtr model = checked_pointer_cast<RenderModel>(renderable_);
			for (uint32_t i = 0; i < model->NumSubrenderables(); ++ i)
			{
				checked_pointer_cast<RenderPolygon>(model->Subrenderable(i))->Wireframe(wf);
			}
		}
	};


	enum
	{
		Exit,
	};

	InputActionDefine actions[] =
	{
		InputActionDefine(Exit, KS_Escape),
	};
}


int SampleMain()
{
	DetailedSurfaceApp app;
	app.Create();
	app.Run();

	return 0;
}

DetailedSurfaceApp::DetailedSurfaceApp()
			: App3DFramework("DetailedSurface"),
				height_scale_(0.06f)
{
	ResLoader::Instance().AddPath("../../Samples/media/DetailedSurface");
}

bool DetailedSurfaceApp::ConfirmDevice() const
{
	return true;
}

void DetailedSurfaceApp::OnCreate()
{
	font_ = SyncLoadFont("gkai00mp.kfont");
	UIManager::Instance().Load(ResLoader::Instance().Open("DetailedSurface.uiml"));

	RenderFactory& rf = Context::Instance().RenderFactoryInstance();
	juda_tex_ = LoadJudaTexture("DetailedSurface.jdt");

	ElementFormat fmt;
	if (rf.RenderEngineInstance().DeviceCaps().texture_format_support(EF_BC1))
	{
		fmt = EF_BC1;
	}
	else
	{
		if (rf.RenderEngineInstance().DeviceCaps().texture_format_support(EF_ABGR8))
		{
			fmt = EF_ABGR8;
		}
		else
		{
			BOOST_ASSERT(rf.RenderEngineInstance().DeviceCaps().texture_format_support(EF_ARGB8));

			fmt = EF_ARGB8;
		}
	}
	juda_tex_->CacheProperty(1024, fmt, 4);

	loading_percentage_ = 0;
}

void DetailedSurfaceApp::OnResize(uint32_t width, uint32_t height)
{
	App3DFramework::OnResize(width, height);

	UIManager::Instance().SettleCtrls();
}

void DetailedSurfaceApp::InputHandler(InputEngine const & /*sender*/, InputAction const & action)
{
	switch (action.first)
	{
	case Exit:
		this->Quit();
		break;
	}
}

void DetailedSurfaceApp::ScaleChangedHandler(KlayGE::UISlider const & sender)
{
	height_scale_ = sender.GetValue() / 100.0f;
	checked_pointer_cast<PolygonObject>(polygon_)->HeightScale(height_scale_);

	std::wostringstream stream;
	stream << L"Scale: " << height_scale_;
	dialog_->Control<UIStatic>(id_scale_static_)->SetText(stream.str());
}

void DetailedSurfaceApp::DetailTypeChangedHandler(KlayGE::UIComboBox const & sender)
{
	checked_pointer_cast<PolygonObject>(polygon_)->DetailType(sender.GetSelectedIndex());
}

void DetailedSurfaceApp::NaLengthHandler(KlayGE::UICheckBox const & sender)
{
	checked_pointer_cast<PolygonObject>(polygon_)->NaLength(sender.GetChecked());
}

void DetailedSurfaceApp::WireframeHandler(KlayGE::UICheckBox const & sender)
{
	checked_pointer_cast<PolygonObject>(polygon_)->Wireframe(sender.GetChecked());
}

void DetailedSurfaceApp::DoUpdateOverlay()
{
	UIManager::Instance().Render();

	std::wostringstream stream;
	stream.precision(2);
	stream << std::fixed << this->FPS() << " FPS";

	font_->RenderText(0, 0, Color(1, 1, 0, 1), L"Detailed Surface", 16);
	font_->RenderText(0, 18, Color(1, 1, 0, 1), stream.str(), 16);

	SceneManager& sceneMgr(Context::Instance().SceneManagerInstance());
	stream.str(L"");
	stream << sceneMgr.NumRenderablesRendered() << " Renderables "
		<< sceneMgr.NumPrimitivesRendered() << " Primitives "
		<< sceneMgr.NumVerticesRendered() << " Vertices";
	font_->RenderText(0, 36, Color(1, 1, 1, 1), stream.str(), 16);
}

uint32_t DetailedSurfaceApp::DoUpdate(uint32_t /*pass*/)
{
	RenderEngine& renderEngine(Context::Instance().RenderFactoryInstance().RenderEngineInstance());
	if (loading_percentage_ < 100)
	{
		UIDialogPtr const & dialog_loading = UIManager::Instance().GetDialog("Loading");
		UIStaticPtr const & msg = dialog_loading->Control<UIStatic>(dialog_loading->IDFromName("Msg"));
		UIProgressBarPtr const & progress_bar = dialog_loading->Control<UIProgressBar>(dialog_loading->IDFromName("Progress"));

		if (loading_percentage_ < 20)
		{
			dialog_ = UIManager::Instance().GetDialog("DetailedSurface");
			dialog_->SetVisible(false);

			dialog_loading->SetVisible(true);

			loading_percentage_ = 20;
			progress_bar->SetValue(loading_percentage_);
			msg->SetText(L"Loading Geometry");
		}
		else if (loading_percentage_ < 60)
		{
			polygon_ = MakeSharedPtr<PolygonObject>();
			checked_pointer_cast<PolygonObject>(polygon_)->BindJudaTexture(juda_tex_);
			juda_tex_->UpdateCache(checked_pointer_cast<PolygonObject>(polygon_)->JudaTexTileIDs(0));
			polygon_->AddToSceneManager();

			this->LookAt(float3(-0.18f, 0.24f, -0.18f), float3(0, 0.05f, 0));
			this->Proj(0.01f, 100);

			tb_controller_.AttachCamera(this->ActiveCamera());
			tb_controller_.Scalers(0.01f, 0.001f);

			loading_percentage_ = 60;
			progress_bar->SetValue(loading_percentage_);
			msg->SetText(L"Loading Light");
		}
		else if (loading_percentage_ < 80)
		{
			light_ = MakeSharedPtr<PointLightSource>();
			light_->Attrib(0);
			light_->Color(float3(2, 2, 2));
			light_->Falloff(float3(1, 0, 1.0f));
			light_->Position(float3(0.25f, 0.5f, -1.0f));
			light_->AddToSceneManager();

			light_proxy_ = MakeSharedPtr<SceneObjectLightSourceProxy>(light_);
			checked_pointer_cast<SceneObjectLightSourceProxy>(light_proxy_)->Scaling(0.01f, 0.01f, 0.01f);
			light_proxy_->AddToSceneManager();

			loading_percentage_ = 80;
			progress_bar->SetValue(loading_percentage_);
			msg->SetText(L"Initalizing Input System");
		}
		else if (loading_percentage_ < 90)
		{
			InputEngine& inputEngine(Context::Instance().InputFactoryInstance().InputEngineInstance());
			InputActionMap actionMap;
			actionMap.AddActions(actions, actions + sizeof(actions) / sizeof(actions[0]));

			action_handler_t input_handler = MakeSharedPtr<input_signal>();
			input_handler->connect(KlayGE::bind(&DetailedSurfaceApp::InputHandler, this, KlayGE::placeholders::_1, KlayGE::placeholders::_2));
			inputEngine.ActionMap(actionMap, input_handler);

			loading_percentage_ = 90;
			progress_bar->SetValue(loading_percentage_);
			msg->SetText(L"Initalizing UI");
		}
		else
		{
			id_scale_static_ = dialog_->IDFromName("ScaleStatic");
			id_scale_slider_ = dialog_->IDFromName("ScaleSlider");
			id_detail_type_static_ = dialog_->IDFromName("DetailTypeStatic");
			id_detail_type_combo_ = dialog_->IDFromName("DetailTypeCombo");
			id_na_length_ = dialog_->IDFromName("NaLength");
			id_wireframe_ = dialog_->IDFromName("Wireframe");

			dialog_->Control<UISlider>(id_scale_slider_)->SetValue(static_cast<int>(height_scale_ * 100));
			dialog_->Control<UISlider>(id_scale_slider_)->OnValueChangedEvent().connect(KlayGE::bind(&DetailedSurfaceApp::ScaleChangedHandler, this, KlayGE::placeholders::_1));
			this->ScaleChangedHandler(*dialog_->Control<UISlider>(id_scale_slider_));

			dialog_->Control<UIComboBox>(id_detail_type_combo_)->SetSelectedByIndex(2);
			dialog_->Control<UIComboBox>(id_detail_type_combo_)->OnSelectionChangedEvent().connect(KlayGE::bind(&DetailedSurfaceApp::DetailTypeChangedHandler, this, KlayGE::placeholders::_1));
			this->DetailTypeChangedHandler(*dialog_->Control<UIComboBox>(id_detail_type_combo_));

			dialog_->Control<UICheckBox>(id_na_length_)->OnChangedEvent().connect(KlayGE::bind(&DetailedSurfaceApp::NaLengthHandler, this, KlayGE::placeholders::_1));
			this->NaLengthHandler(*dialog_->Control<UICheckBox>(id_na_length_));
			dialog_->Control<UICheckBox>(id_wireframe_)->OnChangedEvent().connect(KlayGE::bind(&DetailedSurfaceApp::WireframeHandler, this, KlayGE::placeholders::_1));
			this->WireframeHandler(*dialog_->Control<UICheckBox>(id_wireframe_));

			loading_percentage_ = 100;
			progress_bar->SetValue(loading_percentage_);
			msg->SetText(L"DONE");

			dialog_->SetVisible(true);
			dialog_loading->SetVisible(false);

			RenderDeviceCaps const & caps = Context::Instance().RenderFactoryInstance().RenderEngineInstance().DeviceCaps();
			if (!(caps.hs_support && caps.ds_support))
			{
				dialog_->Control<UIComboBox>(id_detail_type_combo_)->RemoveItem(4);
			}
		}

		renderEngine.CurFrameBuffer()->Clear(FrameBuffer::CBM_Color | FrameBuffer::CBM_Depth, Color(0.0f, 0.0f, 0.0f, 1), 1.0f, 0);
		return App3DFramework::URV_SkipPostProcess | App3DFramework::URV_Finished;
	}
	else
	{
		Color clear_clr(0.2f, 0.4f, 0.6f, 1);
		if (Context::Instance().Config().graphics_cfg.gamma)
		{
			clear_clr.r() = 0.029f;
			clear_clr.g() = 0.133f;
			clear_clr.b() = 0.325f;
		}
		renderEngine.CurFrameBuffer()->Clear(FrameBuffer::CBM_Color | FrameBuffer::CBM_Depth, clear_clr, 1.0f, 0);

		/*float degree(std::clock() / 700.0f);
		float3 lightPos(2, 0, 1);
		float4x4 matRot(MathLib::rotation_y(degree));
		lightPos = MathLib::transform_coord(lightPos, matRot);*/

		float3 light_pos(0.25f, 0.5f, -1.0f);
		light_pos = MathLib::transform_coord(light_pos, this->ActiveCamera().InverseViewMatrix());
		light_pos = MathLib::normalize(light_pos);
		light_->Position(light_pos);

		checked_pointer_cast<PolygonObject>(polygon_)->LightPos(light_->Position());
		checked_pointer_cast<PolygonObject>(polygon_)->LightColor(light_->Color());
		checked_pointer_cast<PolygonObject>(polygon_)->LightFalloff(light_->Falloff());

		return App3DFramework::URV_NeedFlush | App3DFramework::URV_Finished;
	}
}
