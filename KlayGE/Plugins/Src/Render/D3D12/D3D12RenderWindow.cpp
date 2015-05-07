/**
 * @file D3D12RenderWindow.cpp
 * @author Minmin Gong
 *
 * @section DESCRIPTION
 *
 * This source file is part of KlayGE
 * For the latest info, see http://www.klayge.org
 *
 * @section LICENSE
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 * You may alternatively use this source under the terms of
 * the KlayGE Proprietary License (KPL). You can obtained such a license
 * from http://www.klayge.org/licensing/.
 */

#include <KlayGE/KlayGE.hpp>
#include <KFL/ThrowErr.hpp>
#include <KFL/Util.hpp>
#include <KFL/COMPtr.hpp>
#include <KFL/Math.hpp>
#include <KlayGE/SceneManager.hpp>
#include <KlayGE/Context.hpp>
#include <KlayGE/RenderSettings.hpp>
#include <KlayGE/App3D.hpp>
#include <KlayGE/Window.hpp>

#include <vector>
#include <cstring>
#include <boost/assert.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/trim.hpp>
#ifdef KLAYGE_COMPILER_MSVC
#pragma warning(push)
#pragma warning(disable: 4702)
#endif
#include <boost/lexical_cast.hpp>
#ifdef KLAYGE_COMPILER_MSVC
#pragma warning(pop)
#endif

#include <KlayGE/D3D12/D3D12RenderEngine.hpp>
#include <KlayGE/D3D12/D3D12Mapping.hpp>
#include <KlayGE/D3D12/D3D12RenderFactory.hpp>
#include <KlayGE/D3D12/D3D12RenderFactoryInternal.hpp>
#include <KlayGE/D3D12/D3D12RenderView.hpp>
#include <KlayGE/D3D12/D3D12RenderWindow.hpp>

#include "AMDQuadBuffer.hpp"

namespace KlayGE
{
	D3D12RenderWindow::D3D12RenderWindow(IDXGIFactory4Ptr const & gi_factory, D3D12AdapterPtr const & adapter,
			std::string const & name, RenderSettings const & settings)
#ifdef KLAYGE_PLATFORM_WINDOWS_DESKTOP
						: hWnd_(nullptr),
#else
						: metro_d3d_render_win_(ref new MetroD3D12RenderWindow),
#endif
							adapter_(adapter),
							gi_factory_(gi_factory),
							curr_back_buffer_(0)
	{
		// Store info
		name_				= name;
		isFullScreen_		= settings.full_screen;
		sync_interval_		= settings.sync_interval;
#ifdef KLAYGE_PLATFORM_WINDOWS_RUNTIME
		sync_interval_ = std::max(1U, sync_interval_);
#endif

		ElementFormat format = settings.color_fmt;

		WindowPtr const & main_wnd = Context::Instance().AppInstance().MainWnd();
#ifdef KLAYGE_PLATFORM_WINDOWS_DESKTOP
		hWnd_ = main_wnd->HWnd();
#else
		wnd_ = main_wnd->GetWindow();
		metro_d3d_render_win_->BindD3D12RenderWindow(this);
#endif
		on_paint_connect_ = main_wnd->OnPaint().connect(bind(&D3D12RenderWindow::OnPaint, this,
			placeholders::_1));
		on_exit_size_move_connect_ = main_wnd->OnExitSizeMove().connect(bind(&D3D12RenderWindow::OnExitSizeMove, this,
			placeholders::_1));
		on_size_connect_ = main_wnd->OnSize().connect(bind(&D3D12RenderWindow::OnSize, this,
			placeholders::_1, placeholders::_2));
		on_set_cursor_connect_ = main_wnd->OnSetCursor().connect(bind(&D3D12RenderWindow::OnSetCursor, this,
			placeholders::_1));

		if (this->FullScreen())
		{
			left_ = 0;
			top_ = 0;
			width_ = settings.width;
			height_ = settings.height;
		}
		else
		{
			top_ = settings.top;
			left_ = settings.left;
			width_ = main_wnd->Width();
			height_ = main_wnd->Height();
		}

		back_buffer_format_ = D3D12Mapping::MappingFormat(format);

		dxgi_stereo_support_ = gi_factory_->IsWindowedStereoEnabled() ? true : false;

		viewport_->left		= 0;
		viewport_->top		= 0;
		viewport_->width	= width_;
		viewport_->height	= height_;

		ID3D12DevicePtr d3d_12_device;
		ID3D12CommandQueuePtr d3d_12_cmd_queue;
		ID3D11DevicePtr d3d_11_device;
		ID3D11DeviceContextPtr d3d_11_imm_ctx;

		RenderFactory& rf = Context::Instance().RenderFactoryInstance();
		D3D12RenderEngine& d3d12_re = *checked_cast<D3D12RenderEngine*>(&rf.RenderEngineInstance());
		if (d3d12_re.D3D12Device())
		{
			d3d_12_device = d3d12_re.D3D12Device();
			d3d_12_cmd_queue = d3d12_re.D3D12CmdQueue();
			d3d_11_device = d3d12_re.D3D11Device();
			d3d_11_imm_ctx = d3d12_re.D3D11DeviceImmContext();

			main_wnd_ = false;
		}
		else
		{
			UINT create_device_flags = 0;
#ifdef KLAYGE_DEBUG
			create_device_flags |= D3D11_CREATE_DEVICE_DEBUG;

			{
				ID3D12Debug* debug_ctrl;
				TIF(d3d12_re.D3D12GetDebugInterface(IID_ID3D12Debug, reinterpret_cast<void**>(&debug_ctrl)));
				debug_ctrl->EnableDebugLayer();
				debug_ctrl->Release();
			}
#endif

			std::vector<tuple<char const *, D3D_FEATURE_LEVEL, bool> > available_feature_levels;
			available_feature_levels.push_back(KlayGE::make_tuple("11_1", D3D_FEATURE_LEVEL_11_1, true));
			available_feature_levels.push_back(KlayGE::make_tuple("11_0", D3D_FEATURE_LEVEL_11_0, true));
			available_feature_levels.push_back(KlayGE::make_tuple("10_1", D3D_FEATURE_LEVEL_10_1, false));
			available_feature_levels.push_back(KlayGE::make_tuple("10_0", D3D_FEATURE_LEVEL_10_0, false));
			available_feature_levels.push_back(KlayGE::make_tuple("9_3", D3D_FEATURE_LEVEL_9_3, false));
			available_feature_levels.push_back(KlayGE::make_tuple("9_2", D3D_FEATURE_LEVEL_9_2, false));
			available_feature_levels.push_back(KlayGE::make_tuple("9_1", D3D_FEATURE_LEVEL_9_1, false));

			std::vector<std::string> strs;
			boost::algorithm::split(strs, settings.options, boost::is_any_of(","));
			for (size_t index = 0; index < strs.size(); ++ index)
			{
				std::string& opt = strs[index];
				boost::algorithm::trim(opt);
				std::string::size_type loc = opt.find(':');
				std::string opt_name = opt.substr(0, loc);
				std::string opt_val = opt.substr(loc + 1);

				if (0 == strcmp("level", opt_name.c_str()))
				{
					size_t feature_index = 0;
					for (size_t i = 0; i < available_feature_levels.size(); ++ i)
					{
						if (0 == strcmp(get<0>(available_feature_levels[i]), opt_val.c_str()))
						{
							feature_index = i;
							break;
						}
					}

					if (feature_index > 0)
					{
						available_feature_levels.erase(available_feature_levels.begin(),
							available_feature_levels.begin() + feature_index);
					}
				}
			}

			std::vector<D3D_FEATURE_LEVEL> feature_levels_for_12;
			for (size_t i = 0; i < available_feature_levels.size(); ++i)
			{
				if (get<2>(available_feature_levels[i]))
				{
					feature_levels_for_12.push_back(get<1>(available_feature_levels[i]));
				}
			}

			std::vector<D3D_FEATURE_LEVEL> feature_levels_for_11;
			for (size_t i = 0; i < available_feature_levels.size(); ++ i)
			{
				feature_levels_for_11.push_back(get<1>(available_feature_levels[i]));
			}

			std::vector<std::pair<IDXGIAdapter1Ptr, wchar_t const *> > adapters;
			adapters.push_back(std::make_pair(adapter_->DXGIAdapter(), L"HW"));

			{
				IDXGIAdapter1* warp_adapter;
				TIF(gi_factory_->EnumWarpAdapter(IID_IDXGIAdapter1, reinterpret_cast<void**>(&warp_adapter)));
				adapters.push_back(std::make_pair(MakeCOMPtr(warp_adapter), L"WARP"));
			}

			for (size_t j = 0; j < adapters.size(); ++ j)
			{
				IDXGIAdapter* dx_adapter = adapters[j].first.get();
				for (size_t i = 0; i < feature_levels_for_12.size(); ++ i)
				{
					ID3D12Device* device = nullptr;
					if (SUCCEEDED(d3d12_re.D3D12CreateDevice(dx_adapter, feature_levels_for_12[i],
						IID_ID3D12Device, reinterpret_cast<void**>(&device))))
					{
						d3d_12_device = MakeCOMPtr(device);

						if (Context::Instance().AppInstance().ConfirmDevice())
						{
							D3D12_COMMAND_QUEUE_DESC queue_desc;
							queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
							queue_desc.Priority = 0;
							queue_desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
							queue_desc.NodeMask = 0;
							
							ID3D12CommandQueue* cmd_queue;
							TIF(d3d_12_device->CreateCommandQueue(&queue_desc,
								IID_ID3D12CommandQueue, reinterpret_cast<void**>(&cmd_queue)));
							d3d_12_cmd_queue = MakeCOMPtr(cmd_queue);

							ID3D11Device* device_11 = nullptr;
							ID3D11DeviceContext* imm_ctx_11 = nullptr;
							D3D_FEATURE_LEVEL out_feature_level;
							TIF(d3d12_re.D3D11On12CreateDevice(d3d_12_device.get(), create_device_flags,
								&feature_levels_for_11[0], static_cast<uint32_t>(feature_levels_for_11.size()),
								reinterpret_cast<IUnknown**>(&cmd_queue), 1, 0,
								&device_11, &imm_ctx_11, &out_feature_level));
							d3d_11_device = MakeCOMPtr(device_11);
							d3d_11_imm_ctx = MakeCOMPtr(imm_ctx_11);

							d3d12_re.D3DDevice(d3d_12_device, d3d_12_cmd_queue,
								d3d_11_device, d3d_11_imm_ctx, out_feature_level);

							description_ = adapter_->Description() + L" " + adapters[j].second;
							wchar_t const * fl_str;
							switch (out_feature_level)
							{
							case D3D_FEATURE_LEVEL_11_1:
								fl_str = L"11.1";
								break;

							case D3D_FEATURE_LEVEL_11_0:
								fl_str = L"11.0";
								break;

							case D3D_FEATURE_LEVEL_10_1:
								fl_str = L"10.1";
								break;

							case D3D_FEATURE_LEVEL_10_0:
								fl_str = L"10.0";
								break;

							case D3D_FEATURE_LEVEL_9_3:
								fl_str = L"9.3";
								break;

							case D3D_FEATURE_LEVEL_9_2:
								fl_str = L"9.2";
								break;

							case D3D_FEATURE_LEVEL_9_1:
								fl_str = L"9.1";
								break;

							default:
								fl_str = L"Unknown";
								break;
							}
							description_ += L" D3D Level ";
							description_ += fl_str;
							if (settings.sample_count > 1)
							{
								description_ += L" ("
									+ boost::lexical_cast<std::wstring>(settings.sample_count)
									+ L"x AA)";
							}
							break;
						}
						else
						{
							d3d_12_device.reset();
							d3d_12_cmd_queue.reset();
							d3d_11_device.reset();
							d3d_11_imm_ctx.reset();
						}
					}
				}
			}

			main_wnd_ = true;
		}

		Verify(!!d3d_12_device);
		Verify(!!d3d_12_cmd_queue);
		Verify(!!d3d_11_device);
		Verify(!!d3d_11_imm_ctx);

		bool isDepthBuffered = IsDepthFormat(settings.depth_stencil_fmt);
		uint32_t depthBits = NumDepthBits(settings.depth_stencil_fmt);
		uint32_t stencilBits = NumStencilBits(settings.depth_stencil_fmt);
		if (isDepthBuffered)
		{
			BOOST_ASSERT((32 == depthBits) || (24 == depthBits) || (16 == depthBits) || (0 == depthBits));
			BOOST_ASSERT((8 == stencilBits) || (0 == stencilBits));

			UINT format_support;

			if (32 == depthBits)
			{
				BOOST_ASSERT(0 == stencilBits);

				// Try 32-bit zbuffer
				d3d_11_device->CheckFormatSupport(DXGI_FORMAT_D32_FLOAT, &format_support);
				if (format_support & D3D11_FORMAT_SUPPORT_DEPTH_STENCIL)
				{
					depth_stencil_format_ = DXGI_FORMAT_D32_FLOAT;
					stencilBits = 0;
				}
				else
				{
					depthBits = 24;
				}
			}
			if (24 == depthBits)
			{
				d3d_11_device->CheckFormatSupport(DXGI_FORMAT_D24_UNORM_S8_UINT, &format_support);
				if (format_support & D3D11_FORMAT_SUPPORT_DEPTH_STENCIL)
				{
					depth_stencil_format_ = DXGI_FORMAT_D24_UNORM_S8_UINT;
					stencilBits = 8;
				}
				else
				{
					depthBits = 16;
				}
			}
			if (16 == depthBits)
			{
				d3d_11_device->CheckFormatSupport(DXGI_FORMAT_D16_UNORM, &format_support);
				if (format_support & D3D11_FORMAT_SUPPORT_DEPTH_STENCIL)
				{
					depth_stencil_format_ = DXGI_FORMAT_D16_UNORM;
					stencilBits = 0;
				}
				else
				{
					depthBits = 0;
				}
			}
		}

#ifdef KLAYGE_PLATFORM_WINDOWS_DESKTOP
		bool stereo = (STM_LCDShutter == settings.stereo_method) && dxgi_stereo_support_;

		gi_factory_->RegisterStereoStatusWindow(hWnd_, WM_SIZE, &stereo_cookie_);

		std::memset(&sc_desc_, 0, sizeof(sc_desc_));
		sc_desc_.BufferCount = NUM_BACK_BUFFERS;
		sc_desc_.BufferDesc.Width = this->Width();
		sc_desc_.BufferDesc.Height = this->Height();
		sc_desc_.BufferDesc.Format = back_buffer_format_;
		sc_desc_.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
		sc_desc_.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
		sc_desc_.BufferDesc.RefreshRate.Numerator = 60;
		sc_desc_.BufferDesc.RefreshRate.Denominator = 1;
		sc_desc_.SampleDesc.Count = stereo ? 1 : std::min(static_cast<uint32_t>(D3D11_MAX_MULTISAMPLE_SAMPLE_COUNT), settings.sample_count);
		sc_desc_.SampleDesc.Quality = stereo ? 0 : settings.sample_quality;
		sc_desc_.OutputWindow = hWnd_;
		sc_desc_.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		sc_desc_.SwapEffect = stereo ? DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL : DXGI_SWAP_EFFECT_FLIP_DISCARD;
		sc_desc_.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
		sc_desc_.Windowed = !this->FullScreen();
#else
		bool stereo = (STM_LCDShutter == settings.stereo_method) && dxgi_stereo_support_;

		using namespace Windows::Graphics::Display;
		using namespace Windows::Foundation;
		DisplayInformation::GetForCurrentView()->StereoEnabledChanged +=
			ref new TypedEventHandler<DisplayInformation^, Platform::Object^>(metro_d3d_render_win_, &MetroD3D12RenderWindow::OnStereoEnabledChanged);

		std::memset(&sc_desc1_, 0, sizeof(sc_desc1_));
		sc_desc1_.BufferCount = 2;
		sc_desc1_.Width = this->Width();
		sc_desc1_.Height = this->Height();
		sc_desc1_.Format = back_buffer_format_;
		sc_desc1_.Stereo = stereo;
		sc_desc1_.SampleDesc.Count = stereo ? 1 : std::min(static_cast<uint32_t>(D3D11_MAX_MULTISAMPLE_SAMPLE_COUNT), settings.sample_count);
		sc_desc1_.SampleDesc.Quality = stereo ? 0 : settings.sample_quality;
		sc_desc1_.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		sc_desc1_.Scaling = DXGI_SCALING_NONE;
		sc_desc1_.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
		sc_desc1_.Flags = 0;
#endif

#ifdef KLAYGE_PLATFORM_WINDOWS_DESKTOP
		if ((STM_LCDShutter == settings.stereo_method) && !dxgi_stereo_support_)
		{
			if (adapter_->Description().find(L"AMD", 0) != std::wstring::npos)
			{
#ifdef KLAYGE_PLATFORM_WIN64
				HMODULE dll = ::GetModuleHandle(TEXT("atidxx64.dll"));
#else
				HMODULE dll = ::GetModuleHandle(TEXT("atidxx32.dll"));
#endif
				if (dll != nullptr)
				{
					PFNAmdDxExtCreate11 AmdDxExtCreate11 = reinterpret_cast<PFNAmdDxExtCreate11>(::GetProcAddress(dll, "AmdDxExtCreate11"));
					if (AmdDxExtCreate11 != nullptr)
					{
						IAmdDxExt* amd_dx_ext;
						HRESULT hr = AmdDxExtCreate11(d3d_11_device.get(), &amd_dx_ext);
						if (SUCCEEDED(hr))
						{
							AmdDxExtVersion ext_version;
							hr = amd_dx_ext->GetVersion(&ext_version);
							if (SUCCEEDED(hr))
							{
								IAmdDxExtInterface* adti = amd_dx_ext->GetExtInterface(AmdDxExtQuadBufferStereoID);
								IAmdDxExtQuadBufferStereo* qb_stereo = static_cast<IAmdDxExtQuadBufferStereo*>(adti);
								if (qb_stereo != nullptr)
								{
									stereo_amd_qb_ext_ = MakeCOMPtr(qb_stereo);
									stereo_amd_qb_ext_->EnableQuadBufferStereo(true);
								}
							}
						}
						amd_dx_ext->Release();
					}
				}
			}
		}

		IDXGISwapChain* sc = nullptr;
		gi_factory_->CreateSwapChain(d3d_12_cmd_queue.get(), &sc_desc_, &sc);
		swap_chain_ = MakeCOMPtr(sc);

		gi_factory->MakeWindowAssociation(hWnd_, DXGI_MWA_NO_WINDOW_CHANGES | DXGI_MWA_NO_ALT_ENTER);
		swap_chain_->SetFullscreenState(this->FullScreen(), nullptr);
#else
		IDXGISwapChain1* sc = nullptr;
		gi_factory_2_->CreateSwapChainForCoreWindow(d3d_device.get(),
			reinterpret_cast<IUnknown*>(wnd_.Get()), &sc_desc1_, nullptr, &sc);
		IDXGISwapChain2* sc2 = nullptr;
		sc->QueryInterface(IID_IDXGISwapChain2, reinterpret_cast<void**>(&sc2));
		swap_chain_ = MakeCOMPtr(sc2);
		sc->Release();
#endif

		Verify(!!swap_chain_);

		D3D12_DESCRIPTOR_HEAP_DESC desc_heap = {};
		desc_heap.NumDescriptors = NUM_BACK_BUFFERS;
		desc_heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		desc_heap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		ID3D12DescriptorHeap* descriptor_heap;
		TIF(d3d_12_device->CreateDescriptorHeap(&desc_heap, IID_ID3D12DescriptorHeap,
			reinterpret_cast<void**>(&descriptor_heap)));
		desc_heap_ = MakeCOMPtr(descriptor_heap);
		rtv_desc_size_ = 0;

		ID3D12Fence* fence;
		TIF(d3d_12_device->CreateFence(0, D3D12_FENCE_FLAG_NONE,
			IID_ID3D12Fence, reinterpret_cast<void**>(&fence)));
		fence_ = MakeCOMPtr(fence);
		curr_fence_ = 1;

		handle_event_ = ::CreateEventEx(nullptr, FALSE, FALSE, EVENT_ALL_ACCESS);

		this->UpdateSurfacesPtrs();

		this->WaitForPreviousFrame();
	}

	D3D12RenderWindow::~D3D12RenderWindow()
	{
		on_paint_connect_.disconnect();
		on_exit_size_move_connect_.disconnect();
		on_size_connect_.disconnect();
		on_set_cursor_connect_.disconnect();

		this->Destroy();
	}

	std::wstring const & D3D12RenderWindow::Description() const
	{
		return description_;
	}

	// 改变窗口大小
	/////////////////////////////////////////////////////////////////////////////////
	void D3D12RenderWindow::Resize(uint32_t width, uint32_t height)
	{
		width_ = width;
		height_ = height;

		// Notify viewports of resize
		viewport_->width = width;
		viewport_->height = height;

		RenderFactory& rf = Context::Instance().RenderFactoryInstance();
		D3D12RenderEngine& d3d12_re = *checked_cast<D3D12RenderEngine*>(&rf.RenderEngineInstance());
		ID3D11DeviceContextPtr d3d_imm_ctx = d3d12_re.D3D11DeviceImmContext();
		if (d3d_imm_ctx)
		{
			d3d_imm_ctx->ClearState();
		}

		for (size_t i = 0; i < clr_views_.size(); ++ i)
		{
			clr_views_[i].reset();
		}
		rs_view_.reset();

		for (size_t i = 0; i < back_buffers_.size(); ++ i)
		{
			render_target_views_right_eye_[i].reset();
			render_target_views_[i].reset();
			back_buffers_[i].reset();
		}
		depth_stencil_view_.reset();
		depth_stencil_.reset();

		UINT flags = 0;
		if (this->FullScreen())
		{
			flags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
		}

		this->OnUnbind();

		dxgi_stereo_support_ = gi_factory_->IsWindowedStereoEnabled() ? true : false;

		sc_desc_.BufferDesc.Width = width_;
		sc_desc_.BufferDesc.Height = height_;
		//sc_desc_.Stereo = (STM_LCDShutter == Context::Instance().Config().graphics_cfg.stereo_method) && dxgi_stereo_support_;

		if (!!swap_chain_)
		{
			swap_chain_->ResizeBuffers(2, width, height, back_buffer_format_, flags);
		}
		else
		{
			ID3D12CommandQueuePtr const & cmd_queue = d3d12_re.D3D12CmdQueue();

#ifdef KLAYGE_PLATFORM_WINDOWS_DESKTOP
			IDXGISwapChain* sc = nullptr;
			gi_factory_->CreateSwapChain(cmd_queue.get(), &sc_desc_, &sc);
			swap_chain_ = MakeCOMPtr(sc);
			sc->Release();

			swap_chain_->SetFullscreenState(this->FullScreen(), nullptr);
#else
			IDXGISwapChain1* sc = nullptr;
			gi_factory_->CreateSwapChainForCoreWindow(d3d_device.get(),
				reinterpret_cast<IUnknown*>(wnd_.Get()), &sc_desc1_, nullptr, &sc);
			IDXGISwapChain2* sc2 = nullptr;
			sc->QueryInterface(IID_IDXGISwapChain2, reinterpret_cast<void**>(&sc2));
			swap_chain_ = MakeCOMPtr(sc2);
			sc->Release();
#endif

			Verify(!!swap_chain_);
		}

		this->UpdateSurfacesPtrs();

		d3d12_re.ResetRenderStates();

		this->OnBind();

		App3DFramework& app = Context::Instance().AppInstance();
		app.OnResize(width, height);
	}

	// 改变窗口位置
	/////////////////////////////////////////////////////////////////////////////////
	void D3D12RenderWindow::Reposition(uint32_t left, uint32_t top)
	{
		left_ = left;
		top_ = top;
	}

	// 获取是否是全屏状态
	/////////////////////////////////////////////////////////////////////////////////
	bool D3D12RenderWindow::FullScreen() const
	{
		return isFullScreen_;
	}

	// 设置是否是全屏状态
	/////////////////////////////////////////////////////////////////////////////////
	void D3D12RenderWindow::FullScreen(bool fs)
	{
#ifdef KLAYGE_PLATFORM_WINDOWS_DESKTOP
		if (isFullScreen_ != fs)
		{
			left_ = 0;
			top_ = 0;

			uint32_t style;
			if (fs)
			{
				style = WS_POPUP;
			}
			else
			{
				style = WS_OVERLAPPEDWINDOW;
			}

			::SetWindowLongPtrW(hWnd_, GWL_STYLE, style);

			RECT rc = { 0, 0, static_cast<LONG>(width_), static_cast<LONG>(height_) };
			::AdjustWindowRect(&rc, style, false);
			width_ = rc.right - rc.left;
			height_ = rc.bottom - rc.top;
			::SetWindowPos(hWnd_, nullptr, left_, top_, width_, height_, SWP_NOZORDER);

			sc_desc_.BufferDesc.Width = width_;
			sc_desc_.BufferDesc.Height = height_;
			sc_desc_.Windowed = !fs;

			isFullScreen_ = fs;

			swap_chain_->SetFullscreenState(isFullScreen_, nullptr);
			if (isFullScreen_)
			{
				DXGI_MODE_DESC desc;
				std::memset(&desc, 0, sizeof(desc));
				desc.Width = width_;
				desc.Height = height_;
				desc.Format = back_buffer_format_;
				desc.RefreshRate.Numerator = 60;
				desc.RefreshRate.Denominator = 1;
				swap_chain_->ResizeTarget(&desc);
			}

			::ShowWindow(hWnd_, SW_SHOWNORMAL);
			::UpdateWindow(hWnd_);
		}
#else
		UNREF_PARAM(fs);
#endif
	}

	void D3D12RenderWindow::WindowMovedOrResized()
	{
		::RECT rect;
#ifdef KLAYGE_PLATFORM_WINDOWS_DESKTOP
		::GetClientRect(hWnd_, &rect);
#else
		rect.left = static_cast<LONG>(wnd_->Bounds.Left);
		rect.right = static_cast<LONG>(wnd_->Bounds.Right);
		rect.top = static_cast<LONG>(wnd_->Bounds.Top);
		rect.bottom = static_cast<LONG>(wnd_->Bounds.Bottom);
#endif

		uint32_t new_left = rect.left;
		uint32_t new_top = rect.top;
		if ((new_left != left_) || (new_top != top_))
		{
			this->Reposition(new_left, new_top);
		}

		bool stereo_changed = ((gi_factory_->IsWindowedStereoEnabled() ? true : false) != dxgi_stereo_support_);

		uint32_t new_width = rect.right - rect.left;
		uint32_t new_height = rect.bottom - rect.top;
		if ((new_width != width_) || (new_height != height_) || stereo_changed)
		{
			Context::Instance().RenderFactoryInstance().RenderEngineInstance().Resize(new_width, new_height);
		}
	}

	void D3D12RenderWindow::Destroy()
	{
#ifdef KLAYGE_PLATFORM_WINDOWS_DESKTOP
		if (swap_chain_)
		{
			swap_chain_->SetFullscreenState(false, nullptr);
		}

		gi_factory_->UnregisterStereoStatus(stereo_cookie_);
#endif

		for (size_t i = 0; i < back_buffers_.size(); ++ i)
		{
			render_target_views_right_eye_[i].reset();
			render_target_views_[i].reset();
			back_buffers_[i].reset();
		}
		
		depth_stencil_view_right_eye_.reset();
		depth_stencil_view_.reset();
		depth_stencil_.reset();
		swap_chain_.reset();
		gi_factory_.reset();
	}

	void D3D12RenderWindow::UpdateSurfacesPtrs()
	{
		RenderFactory& rf = Context::Instance().RenderFactoryInstance();
		D3D12RenderEngine& d3d12_re = *checked_cast<D3D12RenderEngine*>(&rf.RenderEngineInstance());
		ID3D11DevicePtr d3d_11_device = d3d12_re.D3D11Device();
		ID3D12DevicePtr d3d_12_device = d3d12_re.D3D12Device();

		ID3D11On12Device* d3d_11on12_dev;
		TIF(d3d_11_device->QueryInterface(IID_ID3D11On12Device, reinterpret_cast<void**>(&d3d_11on12_dev)));
		ID3D11On12DevicePtr d3d_11on12_device = MakeCOMPtr(d3d_11on12_dev);

		// Create a render target view
		rtv_desc_size_ = d3d_12_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
		D3D12_CPU_DESCRIPTOR_HANDLE handle = desc_heap_->GetCPUDescriptorHandleForHeapStart();
		for (size_t i = 0; i < back_buffers_.size(); ++ i)
		{
			ID3D12Resource* bb12;
			TIF(swap_chain_->GetBuffer(static_cast<UINT>(i),
				IID_ID3D12Resource, reinterpret_cast<void**>(&bb12)));
			d3d_12_device->CreateRenderTargetView(bb12, nullptr, handle);
			render_targets_[i] = MakeCOMPtr<ID3D12Resource>(bb12);

			D3D11_RESOURCE_FLAGS flags11 = { D3D11_BIND_RENDER_TARGET };
			ID3D11Texture2D* back_buffer;
			TIF(d3d_11on12_device->CreateWrappedResource(bb12, &flags11,
				D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT,
				IID_ID3D11Texture2D, reinterpret_cast<void**>(&back_buffer)));
			back_buffers_[i] = MakeCOMPtr(back_buffer);

			handle.ptr += rtv_desc_size_;
		}
		D3D11_TEXTURE2D_DESC bb_desc;
		back_buffers_[0]->GetDesc(&bb_desc);

		D3D11_RENDER_TARGET_VIEW_DESC rtv_desc;
		rtv_desc.Format = bb_desc.Format;
		if (bb_desc.SampleDesc.Count > 1)
		{
			rtv_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DMSARRAY;
			rtv_desc.Texture2DMSArray.FirstArraySlice = 0;
			rtv_desc.Texture2DMSArray.ArraySize = 1;
		}
		else
		{
			rtv_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
			rtv_desc.Texture2DArray.MipSlice = 0;
			rtv_desc.Texture2DArray.FirstArraySlice = 0;
			rtv_desc.Texture2DArray.ArraySize = 1;
		}

		for (size_t i = 0; i < back_buffers_.size(); ++ i)
		{
			ID3D11RenderTargetView* render_target_view;
			TIF(d3d_11_device->CreateRenderTargetView(back_buffers_[i].get(), &rtv_desc, &render_target_view));
			render_target_views_[i] = MakeCOMPtr(render_target_view);
		}

		bool stereo = (STM_LCDShutter == Context::Instance().Config().graphics_cfg.stereo_method) && dxgi_stereo_support_;

		if (stereo)
		{
			if (bb_desc.SampleDesc.Count > 1)
			{
				rtv_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DMSARRAY;
				rtv_desc.Texture2DMSArray.FirstArraySlice = 1;
				rtv_desc.Texture2DMSArray.ArraySize = 1;
			}
			else
			{
				rtv_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
				rtv_desc.Texture2DArray.MipSlice = 0;
				rtv_desc.Texture2DArray.FirstArraySlice = 1;
				rtv_desc.Texture2DArray.ArraySize = 1;
			}
			for (size_t i = 0; i < back_buffers_.size(); ++ i)
			{
				ID3D11RenderTargetView* render_target_view;
				TIF(d3d_11_device->CreateRenderTargetView(back_buffers_[i].get(), &rtv_desc, &render_target_view));
				render_target_views_right_eye_[i] = MakeCOMPtr(render_target_view);
			}
		}

		// Create depth stencil texture
		D3D11_TEXTURE2D_DESC ds_desc;
		ds_desc.Width = this->Width();
		ds_desc.Height = this->Height();
		ds_desc.MipLevels = 1;
		ds_desc.ArraySize = stereo ? 2 : 1;
		ds_desc.Format = depth_stencil_format_;
		ds_desc.SampleDesc.Count = bb_desc.SampleDesc.Count;
		ds_desc.SampleDesc.Quality = bb_desc.SampleDesc.Quality;
		ds_desc.Usage = D3D11_USAGE_DEFAULT;
		ds_desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
		ds_desc.CPUAccessFlags = 0;
		ds_desc.MiscFlags = 0;
		ID3D11Texture2D* depth_stencil;
		TIF(d3d_11_device->CreateTexture2D(&ds_desc, nullptr, &depth_stencil));
		depth_stencil_ = MakeCOMPtr(depth_stencil);

		// Create the depth stencil view
		D3D11_DEPTH_STENCIL_VIEW_DESC dsv_desc;
		dsv_desc.Format = depth_stencil_format_;
		dsv_desc.Flags = 0;
		if (bb_desc.SampleDesc.Count > 1)
		{
			dsv_desc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DMSARRAY;
			dsv_desc.Texture2DMSArray.FirstArraySlice = 0;
			dsv_desc.Texture2DMSArray.ArraySize = 1;
		}
		else
		{
			dsv_desc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
			dsv_desc.Texture2DArray.MipSlice = 0;
			dsv_desc.Texture2DArray.FirstArraySlice = 0;
			dsv_desc.Texture2DArray.ArraySize = 1;
		}

		ID3D11DepthStencilView* depth_stencil_view;
		TIF(d3d_11_device->CreateDepthStencilView(depth_stencil_.get(), &dsv_desc, &depth_stencil_view));
		depth_stencil_view_ = MakeCOMPtr(depth_stencil_view);

		if (stereo)
		{
			if (bb_desc.SampleDesc.Count > 1)
			{
				dsv_desc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DMSARRAY;
				dsv_desc.Texture2DMSArray.FirstArraySlice = 1;
				dsv_desc.Texture2DMSArray.ArraySize = 1;
			}
			else
			{
				dsv_desc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
				dsv_desc.Texture2DArray.MipSlice = 0;
				dsv_desc.Texture2DArray.FirstArraySlice = 1;
				dsv_desc.Texture2DArray.ArraySize = 1;
			}
			TIF(d3d_11_device->CreateDepthStencilView(depth_stencil_.get(), &dsv_desc, &depth_stencil_view));
			depth_stencil_view_right_eye_ = MakeCOMPtr(depth_stencil_view);
		}

		if (!!stereo_amd_qb_ext_)
		{
			stereo_amd_right_eye_height_ = stereo_amd_qb_ext_->GetLineOffset(swap_chain_.get());
		}

		for (size_t i = 0; i < back_buffers_.size(); ++ i)
		{
			render_target_render_views_[i] = MakeSharedPtr<D3D12RenderTargetRenderView>(render_target_views_[i],
				this->Width(), this->Height(), D3D12Mapping::MappingFormat(back_buffer_format_));
		}
		this->Attach(ATT_Color0, render_target_render_views_[0]);
		if (depth_stencil_view_)
		{
			this->Attach(ATT_DepthStencil, MakeSharedPtr<D3D12DepthStencilRenderView>(depth_stencil_view_,
				this->Width(), this->Height(), D3D12Mapping::MappingFormat(depth_stencil_format_)));
		}
	}

	void D3D12RenderWindow::SwapBuffers()
	{
		if (swap_chain_)
		{
			RenderFactory& rf = Context::Instance().RenderFactoryInstance();
			D3D12RenderEngine& d3d12_re = *checked_cast<D3D12RenderEngine*>(&rf.RenderEngineInstance());
			ID3D11DeviceContextPtr d3d_imm_ctx = d3d12_re.D3D11DeviceImmContext();
			d3d_imm_ctx->Flush();

			TIF(swap_chain_->Present(sync_interval_, 0));
			curr_back_buffer_ = (curr_back_buffer_ + 1) % NUM_BACK_BUFFERS;

			this->WaitForPreviousFrame();
		}
	}

	void D3D12RenderWindow::OnBind()
	{
		this->Attach(ATT_Color0, render_target_render_views_[curr_back_buffer_]);

		D3D12FrameBuffer::OnBind();

		this->WaitForPreviousFrame();
	}

	void D3D12RenderWindow::OnPaint(Window const & win)
	{
		// If we get WM_PAINT messges, it usually means our window was
		// comvered up, so we need to refresh it by re-showing the contents
		// of the current frame.
		if (win.Active() && win.Ready())
		{
			Context::Instance().SceneManagerInstance().Update();
			this->SwapBuffers();
		}
	}

	void D3D12RenderWindow::OnExitSizeMove(Window const & /*win*/)
	{
		this->WindowMovedOrResized();
	}

	void D3D12RenderWindow::OnSize(Window const & win, bool active)
	{
		if (active)
		{
			if (win.Ready())
			{
				this->WindowMovedOrResized();
			}
		}
	}

	void D3D12RenderWindow::OnSetCursor(Window const & /*win*/)
	{
	}

#if defined KLAYGE_PLATFORM_WINDOWS_RUNTIME
	void D3D12RenderWindow::MetroD3D12RenderWindow::OnStereoEnabledChanged(
		Windows::Graphics::Display::DisplayInformation^ /*sender*/, Platform::Object^ /*args*/)
	{
		if ((win_->gi_factory_2_->IsWindowedStereoEnabled() ? true : false) != win_->dxgi_stereo_support_)
		{
			win_->swap_chain_.reset();
			win_->WindowMovedOrResized();
		}
	}

	void D3D12RenderWindow::MetroD3D12RenderWindow::BindD3D12RenderWindow(D3D12RenderWindow* win)
	{
		win_ = win;
	}
#endif

	void D3D12RenderWindow::WaitForPreviousFrame()
	{
		RenderFactory& rf = Context::Instance().RenderFactoryInstance();
		D3D12RenderEngine& d3d12_re = *checked_cast<D3D12RenderEngine*>(&rf.RenderEngineInstance());
		ID3D12CommandQueuePtr cmd_queue = d3d12_re.D3D12CmdQueue();

		uint64_t const fence = curr_fence_;
		TIF(cmd_queue->Signal(fence_.get(), fence));
		++ curr_fence_;

		if (fence_->GetCompletedValue() < fence)
		{
			TIF(fence_->SetEventOnCompletion(fence, handle_event_));
			::WaitForSingleObject(handle_event_, INFINITE);
		}
	}
}
