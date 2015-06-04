/**
 * @file D3D12Texture2D.cpp
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
#include <KFL/COMPtr.hpp>
#include <KFL/ThrowErr.hpp>
#include <KFL/Math.hpp>
#include <KlayGE/Context.hpp>
#include <KlayGE/RenderEngine.hpp>
#include <KlayGE/RenderFactory.hpp>
#include <KFL/Math.hpp>
#include <KlayGE/Texture.hpp>

#include <cstring>

#include <KlayGE/D3D12/D3D12MinGWDefs.hpp>
#include <KlayGE/D3D12/D3D12Typedefs.hpp>
#include <KlayGE/D3D12/D3D12RenderEngine.hpp>
#include <KlayGE/D3D12/D3D12Mapping.hpp>
#include <KlayGE/D3D12/D3D12Texture.hpp>

namespace KlayGE
{
	D3D12Texture2D::D3D12Texture2D(uint32_t width, uint32_t height, uint32_t numMipMaps, uint32_t array_size, ElementFormat format,
						uint32_t sample_count, uint32_t sample_quality, uint32_t access_hint, ElementInitData const * init_data)
					: D3D12Texture(TT_2D, sample_count, sample_quality, access_hint)
	{
		if (0 == numMipMaps)
		{
			numMipMaps = 1;
			uint32_t w = width;
			uint32_t h = height;
			while ((w != 1) || (h != 1))
			{
				++ numMipMaps;

				w = std::max<uint32_t>(1U, w / 2);
				h = std::max<uint32_t>(1U, h / 2);
			}
		}
		num_mip_maps_ = numMipMaps;

		D3D12RenderEngine const & re = *checked_cast<D3D12RenderEngine const *>(&Context::Instance().RenderFactoryInstance().RenderEngineInstance());
		if (re.DeviceFeatureLevel() <= D3D_FEATURE_LEVEL_9_3)
		{
			if ((num_mip_maps_ > 1) && (((width & (width - 1)) != 0) || ((height & (height - 1)) != 0)))
			{
				// height or width is not a power of 2 and multiple mip levels are specified. This is not supported at feature levels below 10.0.
				num_mip_maps_ = 1;
			}

			if ((num_mip_maps_ > 1) && IsCompressedFormat(format))
			{
				// height or width is not a multiply of 4 and multiple mip levels are specified. This is not supported at feature levels below 10.0.
				num_mip_maps_ = 1;
				uint32_t w = width;
				uint32_t h = height;
				while ((w != 1) || (h != 1))
				{
					w = std::max<uint32_t>(1U, w / 2);
					h = std::max<uint32_t>(1U, h / 2);

					if (((w & 0x3) != 0) || ((h & 0x3) != 0))
					{
						break;
					}

					++ num_mip_maps_;
				}
			}
		}

		array_size_ = array_size;
		format_		= format;

		widths_.resize(num_mip_maps_);
		heights_.resize(num_mip_maps_);
		widths_[0] = width;
		heights_[0] = height;
		for (uint32_t level = 1; level < num_mip_maps_; ++ level)
		{
			widths_[level] = std::max<uint32_t>(1U, widths_[level - 1] / 2);
			heights_[level] = std::max<uint32_t>(1U, heights_[level - 1] / 2);
		}

		desc_.Width = width;
		desc_.Height = height;
		desc_.MipLevels = num_mip_maps_;
		desc_.ArraySize = array_size_;
		switch (format_)
		{
		case EF_D16:
			desc_.Format = DXGI_FORMAT_R16_TYPELESS;
			break;

		case EF_D24S8:
			desc_.Format = DXGI_FORMAT_R24G8_TYPELESS;
			break;

		case EF_D32F:
			desc_.Format = DXGI_FORMAT_R32_TYPELESS;
			break;

		default:
			desc_.Format = D3D12Mapping::MappingFormat(format_);
			break;
		}
		desc_.SampleDesc.Count = sample_count;
		desc_.SampleDesc.Quality = sample_quality;

		this->GetD3DFlags(desc_.Usage, desc_.BindFlags, desc_.CPUAccessFlags, desc_.MiscFlags);
		this->ReclaimHWResource(init_data);
	}

	uint32_t D3D12Texture2D::Width(uint32_t level) const
	{
		BOOST_ASSERT(level < num_mip_maps_);

		return widths_[level];
	}

	uint32_t D3D12Texture2D::Height(uint32_t level) const
	{
		BOOST_ASSERT(level < num_mip_maps_);

		return heights_[level];
	}

	void D3D12Texture2D::CopyToTexture(Texture& target)
	{
		BOOST_ASSERT(type_ == target.Type());

		D3D12Texture2D& other(*checked_cast<D3D12Texture2D*>(&target));

		if ((this->Width(0) == target.Width(0)) && (this->Height(0) == target.Height(0)) && (this->Format() == target.Format())
			&& (this->NumMipMaps() == target.NumMipMaps()))
		{
			if ((this->SampleCount() > 1) && (1 == target.SampleCount()))
			{
				for (uint32_t l = 0; l < this->NumMipMaps(); ++ l)
				{
					d3d_imm_ctx_->ResolveSubresource(other.D3DTexture().get(), D3D11CalcSubresource(l, 0, other.NumMipMaps()),
						d3dTexture2D_.get(), D3D11CalcSubresource(l, 0, this->NumMipMaps()), D3D12Mapping::MappingFormat(target.Format()));
				}
			}
			else
			{
				d3d_imm_ctx_->CopyResource(other.D3DTexture().get(), d3dTexture2D_.get());
			}
		}
		else
		{
			uint32_t const array_size = std::min(this->ArraySize(), target.ArraySize());
			uint32_t const num_mips = std::min(this->NumMipMaps(), target.NumMipMaps());
			for (uint32_t index = 0; index < array_size; ++ index)
			{
				for (uint32_t level = 0; level < num_mips; ++ level)
				{
					this->ResizeTexture2D(target, index, level, 0, 0, target.Width(level), target.Height(level),
						index, level, 0, 0, this->Width(level), this->Height(level), true);
				}
			}
		}
	}

	void D3D12Texture2D::CopyToSubTexture2D(Texture& target,
			uint32_t dst_array_index, uint32_t dst_level, uint32_t dst_x_offset, uint32_t dst_y_offset, uint32_t dst_width, uint32_t dst_height,
			uint32_t src_array_index, uint32_t src_level, uint32_t src_x_offset, uint32_t src_y_offset, uint32_t src_width, uint32_t src_height)
	{
		BOOST_ASSERT(type_ == target.Type());

		D3D12Texture& other(*checked_cast<D3D12Texture*>(&target));

		if ((src_width == dst_width) && (src_height == dst_height) && (this->Format() == target.Format()))
		{
			D3D11_BOX src_box;
			src_box.left = src_x_offset;
			src_box.top = src_y_offset;
			src_box.front = 0;
			src_box.right = src_x_offset + src_width;
			src_box.bottom = src_y_offset + src_height;
			src_box.back = 1;

			d3d_imm_ctx_->CopySubresourceRegion(other.D3DResource().get(), D3D11CalcSubresource(dst_level, dst_array_index, target.NumMipMaps()),
				dst_x_offset, dst_y_offset, 0, this->D3DResource().get(), D3D11CalcSubresource(src_level, src_array_index, this->NumMipMaps()), &src_box);
		}
		else
		{
			this->ResizeTexture2D(target, dst_array_index, dst_level, dst_x_offset, dst_y_offset, dst_width, dst_height,
				src_array_index, src_level, src_x_offset, src_y_offset, src_width, src_height, true);
		}
	}

	void D3D12Texture2D::CopyToSubTextureCube(Texture& target,
			uint32_t dst_array_index, CubeFaces dst_face, uint32_t dst_level, uint32_t dst_x_offset, uint32_t dst_y_offset, uint32_t dst_width, uint32_t dst_height,
			uint32_t src_array_index, CubeFaces src_face, uint32_t src_level, uint32_t src_x_offset, uint32_t src_y_offset, uint32_t src_width, uint32_t src_height)
	{
		UNREF_PARAM(src_face);
		BOOST_ASSERT(TT_Cube == target.Type());

		D3D12Texture& other(*checked_cast<D3D12Texture*>(&target));

		if ((src_width == dst_width) && (src_height == dst_height) && (this->Format() == target.Format()))
		{
			D3D11_BOX src_box;
			src_box.left = src_x_offset;
			src_box.top = src_y_offset;
			src_box.front = 0;
			src_box.right = src_x_offset + src_width;
			src_box.bottom = src_y_offset + src_height;
			src_box.back = 1;

			d3d_imm_ctx_->CopySubresourceRegion(other.D3DResource().get(), D3D11CalcSubresource(dst_level, dst_array_index * 6 + dst_face - CF_Positive_X, target.NumMipMaps()),
				dst_x_offset, dst_y_offset, 0, this->D3DResource().get(), D3D11CalcSubresource(src_level, src_array_index, this->NumMipMaps()), &src_box);
		}
		else
		{
			this->ResizeTexture2D(target, dst_array_index * 6 + dst_face - CF_Positive_X, dst_level, dst_x_offset, dst_y_offset, dst_width, dst_height,
				src_array_index, src_level, src_x_offset, src_y_offset, src_width, src_height, true);
		}
	}

	ID3D11ShaderResourceViewPtr const & D3D12Texture2D::RetriveD3DShaderResourceView(uint32_t first_array_index, uint32_t num_items, uint32_t first_level, uint32_t num_levels)
	{
		BOOST_ASSERT(this->AccessHint() & EAH_GPU_Read);

		D3D11_SHADER_RESOURCE_VIEW_DESC desc;
		memset(&desc, 0, sizeof(desc));
		switch (format_)
		{
		case EF_D16:
			desc.Format = DXGI_FORMAT_R16_UNORM;
			break;

		case EF_D24S8:
			desc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
			break;

		case EF_D32F:
			desc.Format = DXGI_FORMAT_R32_FLOAT;
			break;

		default:
			desc.Format = desc_.Format;
			break;
		}

		if (array_size_ > 1)
		{
			if (sample_count_ > 1)
			{
				desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DMSARRAY;
			}
			else
			{
				desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
			}
			desc.Texture2DArray.MostDetailedMip = first_level;
			desc.Texture2DArray.MipLevels = num_levels;
			desc.Texture2DArray.ArraySize = num_items;
			desc.Texture2DArray.FirstArraySlice = first_array_index;
		}
		else
		{
			if (sample_count_ > 1)
			{
				desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DMS;
			}
			else
			{
				desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
			}
			desc.Texture2D.MostDetailedMip = first_level;
			desc.Texture2D.MipLevels = num_levels;
		}

		return this->RetriveD3DSRV(desc);
	}

	ID3D11UnorderedAccessViewPtr const & D3D12Texture2D::RetriveD3DUnorderedAccessView(uint32_t first_array_index, uint32_t num_items, uint32_t level)
	{
		BOOST_ASSERT(this->AccessHint() & EAH_GPU_Unordered);

		D3D11_UNORDERED_ACCESS_VIEW_DESC desc;
		memset(&desc, 0, sizeof(desc));
		desc.Format = desc_.Format;
		if (array_size_ > 1)
		{
			desc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2DARRAY;
			desc.Texture2DArray.MipSlice = level;
			desc.Texture2DArray.ArraySize = num_items;
			desc.Texture2DArray.FirstArraySlice = first_array_index;
		}
		else
		{
			desc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
			desc.Texture2D.MipSlice = level;
		}

		return this->RetriveD3DUAV(desc);
	}

	ID3D11RenderTargetViewPtr const & D3D12Texture2D::RetriveD3DRenderTargetView(uint32_t first_array_index, uint32_t array_size, uint32_t level)
	{
		BOOST_ASSERT(this->AccessHint() & EAH_GPU_Write);
		BOOST_ASSERT(first_array_index < this->ArraySize());
		BOOST_ASSERT(first_array_index + array_size <= this->ArraySize());

		D3D11_RENDER_TARGET_VIEW_DESC desc;
		memset(&desc, 0, sizeof(desc));
		desc.Format = D3D12Mapping::MappingFormat(this->Format());
		if (this->SampleCount() > 1)
		{
			if (this->ArraySize() > 1)
			{
				desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DMSARRAY;
			}
			else
			{
				desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DMS;
			}
		}
		else
		{
			if (this->ArraySize() > 1)
			{
				desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
			}
			else
			{
				desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
			}
		}
		if (this->ArraySize() > 1)
		{
			desc.Texture2DArray.MipSlice = level;
			desc.Texture2DArray.ArraySize = array_size;
			desc.Texture2DArray.FirstArraySlice = first_array_index;
		}
		else
		{
			desc.Texture2D.MipSlice = level;
		}

		return this->RetriveD3DRTV(desc);
	}

	ID3D11DepthStencilViewPtr const & D3D12Texture2D::RetriveD3DDepthStencilView(uint32_t first_array_index, uint32_t array_size, uint32_t level)
	{
		BOOST_ASSERT(this->AccessHint() & EAH_GPU_Write);
		BOOST_ASSERT(first_array_index < this->ArraySize());
		BOOST_ASSERT(first_array_index + array_size <= this->ArraySize());

		D3D11_DEPTH_STENCIL_VIEW_DESC desc;
		memset(&desc, 0, sizeof(desc));
		desc.Format = D3D12Mapping::MappingFormat(this->Format());
		desc.Flags = 0;
		if (this->SampleCount() > 1)
		{
			if (this->ArraySize() > 1)
			{
				desc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DMSARRAY;
			}
			else
			{
				desc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DMS;
			}
		}
		else
		{
			if (this->ArraySize() > 1)
			{
				desc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
			}
			else
			{
				desc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
			}
		}
		if (this->ArraySize() > 1)
		{
			desc.Texture2DArray.MipSlice = level;
			desc.Texture2DArray.ArraySize = array_size;
			desc.Texture2DArray.FirstArraySlice = first_array_index;
		}
		else
		{
			desc.Texture2D.MipSlice = level;
		}

		return this->RetriveD3DDSV(desc);
	}

	void D3D12Texture2D::Map2D(uint32_t array_index, uint32_t level, TextureMapAccess tma,
			uint32_t x_offset, uint32_t y_offset, uint32_t /*width*/, uint32_t /*height*/,
			void*& data, uint32_t& row_pitch)
	{
		D3D11_MAPPED_SUBRESOURCE mapped;
		TIF(d3d_imm_ctx_->Map(d3dTexture2D_.get(), D3D11CalcSubresource(level, array_index, num_mip_maps_), D3D12Mapping::Mapping(tma, type_, access_hint_, num_mip_maps_), 0, &mapped));
		uint8_t* p = static_cast<uint8_t*>(mapped.pData);
		data = p + y_offset * mapped.RowPitch + x_offset * NumFormatBytes(format_);
		row_pitch = mapped.RowPitch;
	}

	void D3D12Texture2D::Unmap2D(uint32_t array_index, uint32_t level)
	{
		d3d_imm_ctx_->Unmap(d3dTexture2D_.get(), D3D11CalcSubresource(level, array_index, num_mip_maps_));
	}

	void D3D12Texture2D::BuildMipSubLevels()
	{
		if (d3d_sr_views_.empty())
		{
			for (uint32_t index = 0; index < this->ArraySize(); ++ index)
			{
				for (uint32_t level = 1; level < this->NumMipMaps(); ++ level)
				{
					this->ResizeTexture2D(*this, index, level, 0, 0, this->Width(level), this->Height(level),
						index, level - 1, 0, 0, this->Width(level - 1), this->Height(level - 1), true);
				}
			}
		}
		else
		{
			BOOST_ASSERT(access_hint_ & EAH_Generate_Mips);
			d3d_imm_ctx_->GenerateMips(d3d_sr_views_.begin()->second.get());
		}
	}

	void D3D12Texture2D::OfferHWResource()
	{
		d3d_sr_views_.clear();
		d3d_ua_views_.clear();
		d3d_rt_views_.clear();
		d3d_ds_views_.clear();
		d3dTexture2D_.reset();
	}

	void D3D12Texture2D::ReclaimHWResource(ElementInitData const * init_data)
	{
		std::vector<D3D11_SUBRESOURCE_DATA> subres_data(array_size_ * num_mip_maps_);
		if (init_data != nullptr)
		{
			for (uint32_t j = 0; j < array_size_; ++ j)
			{
				for (uint32_t i = 0; i < num_mip_maps_; ++ i)
				{
					subres_data[j * num_mip_maps_ + i].pSysMem = init_data[j * num_mip_maps_ + i].data;
					subres_data[j * num_mip_maps_ + i].SysMemPitch = init_data[j * num_mip_maps_ + i].row_pitch;
					subres_data[j * num_mip_maps_ + i].SysMemSlicePitch = init_data[j * num_mip_maps_ + i].slice_pitch;
				}
			}
		}

		D3D12RenderEngine const & re = *checked_cast<D3D12RenderEngine const *>(&Context::Instance().RenderFactoryInstance().RenderEngineInstance());
		ID3D11DevicePtr d3d_11_device = re.D3D11Device();
		ID3D12DevicePtr d3d_12_device = re.D3D12Device();
		ID3D12CommandAllocatorPtr d3d_12_cmd_allocator = re.D3D12CmdAllocator();

		D3D12_RESOURCE_DESC tex_desc = {};
		tex_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		tex_desc.Alignment = 0;
		tex_desc.Width = desc_.Width;
		tex_desc.Height = desc_.Height;
		tex_desc.DepthOrArraySize = static_cast<UINT16>(desc_.ArraySize);
		tex_desc.MipLevels = static_cast<UINT16>(desc_.MipLevels);
		tex_desc.Format = desc_.Format;
		tex_desc.SampleDesc.Count = 1;
		tex_desc.SampleDesc.Quality = 0;
		tex_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
		tex_desc.Flags = D3D12_RESOURCE_FLAG_NONE;
		if (access_hint_ & EAH_GPU_Write)
		{
			if (IsDepthFormat(format_))
			{
				tex_desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
			}
			else
			{
				tex_desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
			}
		}
		if (access_hint_ & EAH_GPU_Unordered)
		{
			tex_desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
		}

		D3D12_HEAP_PROPERTIES heap_prop;
		heap_prop.Type = D3D12_HEAP_TYPE_DEFAULT;
		heap_prop.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
		heap_prop.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
		heap_prop.CreationNodeMask = 1;
		heap_prop.VisibleNodeMask = 1;

		ID3D12Resource* d3d_12_texture;
		TIF(d3d_12_device->CreateCommittedResource(&heap_prop, D3D12_HEAP_FLAG_NONE,
			&tex_desc, D3D12_RESOURCE_STATE_COMMON, nullptr,
			IID_ID3D12Resource, reinterpret_cast<void**>(&d3d_12_texture)));
		d3d_12_texture_ = MakeCOMPtr(d3d_12_texture);

		uint32_t const num_subres = array_size_ * num_mip_maps_;
		uint64_t upload_buffer_size = 0;
		d3d_12_device->GetCopyableFootprints(&tex_desc, 0, num_subres, 0, nullptr, nullptr, nullptr, &upload_buffer_size);

		D3D12_HEAP_PROPERTIES upload_heap_prop;
		upload_heap_prop.Type = D3D12_HEAP_TYPE_UPLOAD;
		upload_heap_prop.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
		upload_heap_prop.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
		upload_heap_prop.CreationNodeMask = 1;
		upload_heap_prop.VisibleNodeMask = 1;

		D3D12_RESOURCE_DESC buff_desc;
		buff_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		buff_desc.Alignment = 0;
		buff_desc.Width = upload_buffer_size;
		buff_desc.Height = 1;
		buff_desc.DepthOrArraySize = 1;
		buff_desc.MipLevels = 1;
		buff_desc.Format = DXGI_FORMAT_UNKNOWN;
		buff_desc.SampleDesc.Count = 1;
		buff_desc.SampleDesc.Quality = 0;
		buff_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		buff_desc.Flags = D3D12_RESOURCE_FLAG_NONE;

		ID3D12Resource* d3d_12_texture_upload_heaps;
		TIF(d3d_12_device->CreateCommittedResource(&upload_heap_prop, D3D12_HEAP_FLAG_NONE, &buff_desc,
			D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
			IID_ID3D12Resource, reinterpret_cast<void**>(&d3d_12_texture_upload_heaps)));
		d3d_12_texture_upload_heaps_ = MakeCOMPtr(d3d_12_texture_upload_heaps);

		if (init_data != nullptr)
		{
			ID3D12CommandQueuePtr d3d_12_cmd_queue = re.D3D12CmdQueue();

			ID3D12GraphicsCommandList* cmd_list;
			TIF(d3d_12_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, d3d_12_cmd_allocator.get(), nullptr,
				IID_ID3D12GraphicsCommandList, reinterpret_cast<void**>(&cmd_list)));
			ID3D12GraphicsCommandListPtr d3d_12_cmd_list = MakeCOMPtr(cmd_list);

			D3D12_RESOURCE_BARRIER barrier_before;
			barrier_before.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			barrier_before.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
			barrier_before.Transition.pResource = d3d_12_texture_.get();
			barrier_before.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
			barrier_before.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
			barrier_before.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

			d3d_12_cmd_list->ResourceBarrier(1, &barrier_before);

			std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> layouts(num_subres);
			std::vector<uint64_t> row_sizes_in_bytes(num_subres);
			std::vector<uint32_t> num_rows(num_subres);

			uint64_t required_size = 0;
			d3d_12_device->GetCopyableFootprints(&tex_desc, 0, num_subres, 0, &layouts[0], &num_rows[0], &row_sizes_in_bytes[0], &required_size);

			uint8_t* p;
			d3d_12_texture_upload_heaps_->Map(0, nullptr, reinterpret_cast<void**>(&p));
			for (uint32_t i = 0; i < num_subres; ++ i)
			{
				D3D12_SUBRESOURCE_DATA src_data;
				src_data.pData = subres_data[i].pSysMem;
				src_data.RowPitch = subres_data[i].SysMemPitch;
				src_data.SlicePitch = subres_data[i].SysMemSlicePitch;

				D3D12_MEMCPY_DEST dest_data;
				dest_data.pData = p + layouts[i].Offset;
				dest_data.RowPitch = layouts[i].Footprint.RowPitch;
				dest_data.SlicePitch = layouts[i].Footprint.RowPitch * num_rows[i];

				for (UINT z = 0; z < layouts[i].Footprint.Depth; ++ z)
				{
					uint8_t const * src_slice
						= reinterpret_cast<uint8_t const *>(src_data.pData) + src_data.SlicePitch * z;
					uint8_t* dest_slice = reinterpret_cast<uint8_t*>(dest_data.pData) + dest_data.SlicePitch * z;
					for (UINT y = 0; y < num_rows[i]; ++ y)
					{
						memcpy(dest_slice + dest_data.RowPitch * y, src_slice + src_data.RowPitch * y,
							row_sizes_in_bytes[i]);
					}
				}
			}
			d3d_12_texture_upload_heaps_->Unmap(0, nullptr);

			for (uint32_t i = 0; i < num_subres; ++ i)
			{
				D3D12_TEXTURE_COPY_LOCATION src;
				src.pResource = d3d_12_texture_upload_heaps_.get();
				src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
				src.PlacedFootprint = layouts[i];

				D3D12_TEXTURE_COPY_LOCATION dst;
				dst.pResource = d3d_12_texture_.get();
				dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
				dst.SubresourceIndex = i;

				cmd_list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
			}

			D3D12_RESOURCE_BARRIER barrier_after;
			barrier_after.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			barrier_after.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
			barrier_after.Transition.pResource = d3d_12_texture_.get();
			barrier_after.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
			barrier_after.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE; // TODO
			barrier_after.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

			d3d_12_cmd_list->ResourceBarrier(1, &barrier_after);

			TIF(d3d_12_cmd_list->Close());
			ID3D12CommandList* cmd_lists[] = { d3d_12_cmd_list.get() };
			d3d_12_cmd_queue->ExecuteCommandLists(sizeof(cmd_lists) / sizeof(cmd_lists[0]), cmd_lists);
		}

		ID3D11On12Device* d3d_11on12_dev;
		TIF(d3d_11_device->QueryInterface(IID_ID3D11On12Device, reinterpret_cast<void**>(&d3d_11on12_dev)));
		ID3D11On12DevicePtr d3d_11on12_device = MakeCOMPtr(d3d_11on12_dev);

		D3D11_RESOURCE_FLAGS flags11;
		D3D11_USAGE usage;
		this->GetD3DFlags(usage, flags11.BindFlags, flags11.CPUAccessFlags, flags11.MiscFlags);
		flags11.CPUAccessFlags = 0;
		flags11.StructureByteStride = 0;
		D3D12_RESOURCE_STATES res_state = D3D12_RESOURCE_STATE_COMMON;
		if ((access_hint_ & EAH_GPU_Read) || (D3D11_USAGE_DYNAMIC == usage))
		{
			res_state |= D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
			if (IsDepthFormat(format_))
			{
				res_state |= D3D12_RESOURCE_STATE_DEPTH_READ;
			}
		}
		if (access_hint_ & EAH_GPU_Write)
		{
			if (IsDepthFormat(format_))
			{
				res_state |= D3D12_RESOURCE_STATE_DEPTH_WRITE;
			}
			else
			{
				res_state |= D3D12_RESOURCE_STATE_RENDER_TARGET;
			}
		}
		if (access_hint_ & EAH_GPU_Unordered)
		{
			res_state |= D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
		}
		ID3D11Texture2D* d3d_tex;
		TIF(d3d_11on12_device->CreateWrappedResource(d3d_12_texture_.get(), &flags11, res_state, res_state,
			IID_ID3D11Texture2D, reinterpret_cast<void**>(&d3d_tex)));
		d3dTexture2D_ = MakeCOMPtr(d3d_tex);

		/*ID3D11Texture2D* d3d_tex;
		TIF(d3d_device_->CreateTexture2D(&desc_, (init_data != nullptr) ? &subres_data[0] : nullptr, &d3d_tex));
		d3dTexture2D_ = MakeCOMPtr(d3d_tex);*/

		if ((access_hint_ & (EAH_GPU_Read | EAH_Generate_Mips)) && (num_mip_maps_ > 1))
		{
			this->RetriveD3DShaderResourceView(0, array_size_, 0, num_mip_maps_);
		}
	}
}
