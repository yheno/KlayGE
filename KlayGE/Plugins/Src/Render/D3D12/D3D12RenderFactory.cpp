/**
 * @file D3D12RenderFactory.cpp
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
#include <KFL/Util.hpp>
#include <KFL/Math.hpp>

#include <KlayGE/D3D12/D3D12RenderEngine.hpp>
#include <KlayGE/D3D12/D3D12Texture.hpp>
#include <KlayGE/D3D12/D3D12FrameBuffer.hpp>
#include <KlayGE/D3D12/D3D12RenderLayout.hpp>
#include <KlayGE/D3D12/D3D12GraphicsBuffer.hpp>
#include <KlayGE/D3D12/D3D12Query.hpp>
#include <KlayGE/D3D12/D3D12RenderView.hpp>
#include <KlayGE/D3D12/D3D12RenderStateObject.hpp>
#include <KlayGE/D3D12/D3D12ShaderObject.hpp>

#include <KlayGE/D3D12/D3D12RenderFactory.hpp>
#include <KlayGE/D3D12/D3D12RenderFactoryInternal.hpp>

namespace KlayGE
{
	D3D12RenderFactory::D3D12RenderFactory()
	{
	}

	std::wstring const & D3D12RenderFactory::Name() const
	{
		static std::wstring const name(L"Direct3D12 Render Factory");
		return name;
	}

	TexturePtr D3D12RenderFactory::MakeTexture1D(uint32_t width, uint32_t numMipMaps, uint32_t array_size,
			ElementFormat format, uint32_t sample_count, uint32_t sample_quality, uint32_t access_hint, ElementInitData const * init_data)
	{
		return MakeSharedPtr<D3D12Texture1D>(width, numMipMaps, array_size, format, sample_count, sample_quality, access_hint, init_data);
	}
	TexturePtr D3D12RenderFactory::MakeTexture2D(uint32_t width, uint32_t height, uint32_t numMipMaps, uint32_t array_size,
			ElementFormat format, uint32_t sample_count, uint32_t sample_quality, uint32_t access_hint, ElementInitData const * init_data)
	{
		return MakeSharedPtr<D3D12Texture2D>(width, height, numMipMaps, array_size, format, sample_count, sample_quality, access_hint, init_data);
	}
	TexturePtr D3D12RenderFactory::MakeTexture3D(uint32_t width, uint32_t height, uint32_t depth, uint32_t numMipMaps, uint32_t array_size,
			ElementFormat format, uint32_t sample_count, uint32_t sample_quality, uint32_t access_hint, ElementInitData const * init_data)
	{
		return MakeSharedPtr<D3D12Texture3D>(width, height, depth, numMipMaps, array_size, format, sample_count, sample_quality, access_hint, init_data);
	}
	TexturePtr D3D12RenderFactory::MakeTextureCube(uint32_t size, uint32_t numMipMaps, uint32_t array_size,
			ElementFormat format, uint32_t sample_count, uint32_t sample_quality, uint32_t access_hint, ElementInitData const * init_data)
	{
		return MakeSharedPtr<D3D12TextureCube>(size, numMipMaps, array_size, format, sample_count, sample_quality, access_hint, init_data);
	}

	FrameBufferPtr D3D12RenderFactory::MakeFrameBuffer()
	{
		return MakeSharedPtr<D3D12FrameBuffer>();
	}

	RenderLayoutPtr D3D12RenderFactory::MakeRenderLayout()
	{
		return MakeSharedPtr<D3D12RenderLayout>();
	}

	GraphicsBufferPtr D3D12RenderFactory::MakeVertexBuffer(BufferUsage usage, uint32_t access_hint, ElementInitData const * init_data, ElementFormat fmt)
	{
		return MakeSharedPtr<D3D12GraphicsBuffer>(usage, access_hint, D3D11_BIND_VERTEX_BUFFER, init_data, fmt);
	}

	GraphicsBufferPtr D3D12RenderFactory::MakeIndexBuffer(BufferUsage usage, uint32_t access_hint, ElementInitData const * init_data, ElementFormat fmt)
	{
		return MakeSharedPtr<D3D12GraphicsBuffer>(usage, access_hint, D3D11_BIND_INDEX_BUFFER, init_data, fmt);
	}

	GraphicsBufferPtr D3D12RenderFactory::MakeConstantBuffer(BufferUsage usage, uint32_t access_hint, ElementInitData const * init_data, ElementFormat fmt)
	{
		return MakeSharedPtr<D3D12GraphicsBuffer>(usage, access_hint, D3D11_BIND_CONSTANT_BUFFER, init_data, fmt);
	}

	QueryPtr D3D12RenderFactory::MakeOcclusionQuery()
	{
		return MakeSharedPtr<D3D12OcclusionQuery>();
	}

	QueryPtr D3D12RenderFactory::MakeConditionalRender()
	{
		RenderEngine& re = Context::Instance().RenderFactoryInstance().RenderEngineInstance();
		RenderDeviceCaps const & caps = re.DeviceCaps();
		if (caps.max_shader_model >= ShaderModel(4, 0))
		{
			return MakeSharedPtr<D3D12ConditionalRender>();
		}
		else
		{
			return QueryPtr();
		}
	}

	QueryPtr D3D12RenderFactory::MakeTimerQuery()
	{
		D3D12RenderEngine& re = *checked_cast<D3D12RenderEngine*>(&Context::Instance().RenderFactoryInstance().RenderEngineInstance());
		if (re.DeviceFeatureLevel() >= D3D_FEATURE_LEVEL_10_0)
		{
			return MakeSharedPtr<D3D12TimerQuery>();
		}
		else
		{
			return QueryPtr();
		}
	}

	RenderViewPtr D3D12RenderFactory::Make1DRenderView(Texture& texture, int first_array_index, int array_size, int level)
	{
		return MakeSharedPtr<D3D12RenderTargetRenderView>(texture, first_array_index, array_size, level);
	}

	RenderViewPtr D3D12RenderFactory::Make2DRenderView(Texture& texture, int first_array_index, int array_size, int level)
	{
		return MakeSharedPtr<D3D12RenderTargetRenderView>(texture, first_array_index, array_size, level);
	}

	RenderViewPtr D3D12RenderFactory::Make2DRenderView(Texture& texture, int array_index, Texture::CubeFaces face, int level)
	{
		return MakeSharedPtr<D3D12RenderTargetRenderView>(texture, array_index, face, level);
	}

	RenderViewPtr D3D12RenderFactory::Make2DRenderView(Texture& texture, int array_index, uint32_t slice, int level)
	{
		return this->Make3DRenderView(texture, array_index, slice, 1, level);
	}

	RenderViewPtr D3D12RenderFactory::MakeCubeRenderView(Texture& texture, int array_index, int level)
	{
		int array_size = 1;
		return MakeSharedPtr<D3D12RenderTargetRenderView>(texture, array_index, array_size, level);
	}

	RenderViewPtr D3D12RenderFactory::Make3DRenderView(Texture& texture, int array_index, uint32_t first_slice, uint32_t num_slices, int level)
	{
		return MakeSharedPtr<D3D12RenderTargetRenderView>(texture, array_index, first_slice, num_slices, level);
	}

	RenderViewPtr D3D12RenderFactory::MakeGraphicsBufferRenderView(GraphicsBuffer& gbuffer,
		uint32_t width, uint32_t height, ElementFormat pf)
	{
		return MakeSharedPtr<D3D12RenderTargetRenderView>(gbuffer, width, height, pf);
	}

	RenderViewPtr D3D12RenderFactory::Make2DDepthStencilRenderView(uint32_t width, uint32_t height,
		ElementFormat pf, uint32_t sample_count, uint32_t sample_quality)
	{
		return MakeSharedPtr<D3D12DepthStencilRenderView>(width, height, pf, sample_count, sample_quality);
	}

	RenderViewPtr D3D12RenderFactory::Make1DDepthStencilRenderView(Texture& texture, int first_array_index, int array_size, int level)
	{
		return MakeSharedPtr<D3D12DepthStencilRenderView>(texture, first_array_index, array_size, level);
	}

	RenderViewPtr D3D12RenderFactory::Make2DDepthStencilRenderView(Texture& texture, int first_array_index, int array_size, int level)
	{
		return MakeSharedPtr<D3D12DepthStencilRenderView>(texture, first_array_index, array_size, level);
	}

	RenderViewPtr D3D12RenderFactory::Make2DDepthStencilRenderView(Texture& texture, int array_index, Texture::CubeFaces face, int level)
	{
		return MakeSharedPtr<D3D12DepthStencilRenderView>(texture, array_index, face, level);
	}
	
	RenderViewPtr D3D12RenderFactory::Make2DDepthStencilRenderView(Texture& texture, int array_index, uint32_t slice, int level)
	{
		return this->Make3DDepthStencilRenderView(texture, array_index, slice, 1, level);
	}

	RenderViewPtr D3D12RenderFactory::MakeCubeDepthStencilRenderView(Texture& texture, int array_index, int level)
	{
		int array_size = 1;
		return MakeSharedPtr<D3D12DepthStencilRenderView>(texture, array_index, array_size, level);
	}
	
	RenderViewPtr D3D12RenderFactory::Make3DDepthStencilRenderView(Texture& texture, int array_index, uint32_t first_slice, uint32_t num_slices, int level)
	{
		return MakeSharedPtr<D3D12DepthStencilRenderView>(texture, array_index, first_slice, num_slices, level);
	}

	UnorderedAccessViewPtr D3D12RenderFactory::Make1DUnorderedAccessView(Texture& texture, int first_array_index, int array_size, int level)
	{
		return MakeSharedPtr<D3D12UnorderedAccessView>(texture, first_array_index, array_size, level);
	}

	UnorderedAccessViewPtr D3D12RenderFactory::Make2DUnorderedAccessView(Texture& texture, int first_array_index, int array_size, int level)
	{
		return MakeSharedPtr<D3D12UnorderedAccessView>(texture, first_array_index, array_size, level);
	}

	UnorderedAccessViewPtr D3D12RenderFactory::Make2DUnorderedAccessView(Texture& texture, int array_index, Texture::CubeFaces face, int level)
	{
		return MakeSharedPtr<D3D12UnorderedAccessView>(texture, array_index, face, level);
	}

	UnorderedAccessViewPtr D3D12RenderFactory::Make2DUnorderedAccessView(Texture& texture, int array_index, uint32_t slice, int level)
	{
		return MakeSharedPtr<D3D12UnorderedAccessView>(texture, array_index, slice, level);
	}

	UnorderedAccessViewPtr D3D12RenderFactory::MakeCubeUnorderedAccessView(Texture& texture, int array_index, int level)
	{
		int array_size = 1;
		return MakeSharedPtr<D3D12UnorderedAccessView>(texture, array_index, array_size, level);
	}

	UnorderedAccessViewPtr D3D12RenderFactory::Make3DUnorderedAccessView(Texture& texture, int array_index, uint32_t first_slice, uint32_t num_slices, int level)
	{
		return MakeSharedPtr<D3D12UnorderedAccessView>(texture, array_index, first_slice, num_slices, level);
	}

	UnorderedAccessViewPtr D3D12RenderFactory::MakeGraphicsBufferUnorderedAccessView(GraphicsBuffer& gbuffer, ElementFormat pf)
	{
		return MakeSharedPtr<D3D12UnorderedAccessView>(gbuffer, pf);
	}

	ShaderObjectPtr D3D12RenderFactory::MakeShaderObject()
	{
		return MakeSharedPtr<D3D12ShaderObject>();
	}

	RenderEnginePtr D3D12RenderFactory::DoMakeRenderEngine()
	{
		return MakeSharedPtr<D3D12RenderEngine>();
	}

	RasterizerStateObjectPtr D3D12RenderFactory::DoMakeRasterizerStateObject(RasterizerStateDesc const & desc)
	{
		return MakeSharedPtr<D3D12RasterizerStateObject>(desc);
	}

	DepthStencilStateObjectPtr D3D12RenderFactory::DoMakeDepthStencilStateObject(DepthStencilStateDesc const & desc)
	{
		return MakeSharedPtr<D3D12DepthStencilStateObject>(desc);
	}

	BlendStateObjectPtr D3D12RenderFactory::DoMakeBlendStateObject(BlendStateDesc const & desc)
	{
		return MakeSharedPtr<D3D12BlendStateObject>(desc);
	}

	SamplerStateObjectPtr D3D12RenderFactory::DoMakeSamplerStateObject(SamplerStateDesc const & desc)
	{
		return MakeSharedPtr<D3D12SamplerStateObject>(desc);
	}

	void D3D12RenderFactory::DoSuspend()
	{
		// TODO
	}

	void D3D12RenderFactory::DoResume()
	{
		// TODO
	}
}

void MakeRenderFactory(KlayGE::RenderFactoryPtr& ptr)
{
	ptr = KlayGE::MakeSharedPtr<KlayGE::D3D12RenderFactory>();
}
