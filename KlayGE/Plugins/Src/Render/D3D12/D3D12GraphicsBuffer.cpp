/**
 * @file D3D12GraphicsBuffer.cpp
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
#include <KlayGE/RenderEngine.hpp>
#include <KlayGE/RenderFactory.hpp>
#include <KlayGE/Context.hpp>

#include <algorithm>
#include <boost/assert.hpp>

#include <KlayGE/D3D12/D3D12RenderEngine.hpp>
#include <KlayGE/D3D12/D3D12Mapping.hpp>
#include <KlayGE/D3D12/D3D12GraphicsBuffer.hpp>

namespace KlayGE
{
	D3D12GraphicsBuffer::D3D12GraphicsBuffer(BufferUsage usage, uint32_t access_hint, uint32_t bind_flags, ElementInitData const * init_data, ElementFormat fmt)
						: GraphicsBuffer(usage, access_hint),
							bind_flags_(bind_flags), fmt_as_shader_res_(fmt)
	{
		if ((access_hint_ & EAH_GPU_Unordered) && (fmt_as_shader_res_ != EF_Unknown))
		{
			bind_flags_ = 0;
		}

		D3D12RenderEngine const & renderEngine(*checked_cast<D3D12RenderEngine const *>(&Context::Instance().RenderFactoryInstance().RenderEngineInstance()));
		d3d_device_ = renderEngine.D3D11Device();
		d3d_imm_ctx_ = renderEngine.D3D11DeviceImmContext();
		size_in_byte_ = 0;

		if (init_data != nullptr)
		{
			size_in_byte_ = init_data->row_pitch;

			D3D11_SUBRESOURCE_DATA subres_init;
			subres_init.pSysMem = init_data->data;
			subres_init.SysMemPitch = init_data->row_pitch;
			subres_init.SysMemSlicePitch = init_data->slice_pitch;

			this->CreateBuffer(&subres_init);
			hw_buff_size_ = size_in_byte_;
		}
	}

	void D3D12GraphicsBuffer::GetD3DFlags(D3D11_USAGE& usage, UINT& cpu_access_flags, UINT& bind_flags, UINT& misc_flags)
	{
		if (access_hint_ & EAH_Immutable)
		{
			usage = D3D11_USAGE_IMMUTABLE;
		}
		else
		{
			if ((EAH_CPU_Write == access_hint_) || ((EAH_CPU_Write | EAH_GPU_Read) == access_hint_))
			{
				usage = D3D11_USAGE_DYNAMIC;
			}
			else
			{
				if (!(access_hint_ & EAH_CPU_Read) && !(access_hint_ & EAH_CPU_Write))
				{
					usage = D3D11_USAGE_DEFAULT;
				}
				else
				{
					usage = D3D11_USAGE_STAGING;
				}
			}
		}

		cpu_access_flags = 0;
		if (access_hint_ & EAH_CPU_Read)
		{
			cpu_access_flags |= D3D11_CPU_ACCESS_READ;
		}
		if (access_hint_ & EAH_CPU_Write)
		{
			cpu_access_flags |= D3D11_CPU_ACCESS_WRITE;
		}

		if (D3D11_USAGE_STAGING == usage)
		{
			bind_flags = 0;
		}
		else
		{
			bind_flags = bind_flags_;
		}
		D3D12RenderEngine const & re = *checked_cast<D3D12RenderEngine const *>(&Context::Instance().RenderFactoryInstance().RenderEngineInstance());
		if (re.DeviceFeatureLevel() > D3D_FEATURE_LEVEL_9_3)
		{
			if (access_hint_ & EAH_GPU_Read)
			{
				bind_flags |= D3D11_BIND_SHADER_RESOURCE;
			}
			if (access_hint_ & EAH_GPU_Write)
			{
				if (!((access_hint_ & EAH_GPU_Structured) || (access_hint_ & EAH_GPU_Unordered)))
				{
					bind_flags |= D3D11_BIND_STREAM_OUTPUT;
				}
			}
			if (access_hint_ & EAH_GPU_Unordered)
			{
				bind_flags |= D3D11_BIND_UNORDERED_ACCESS;
			}
		}

		misc_flags = 0;
		if (access_hint_ & EAH_GPU_Unordered)
		{
			misc_flags |= (access_hint_ & EAH_GPU_Structured)
				? D3D11_RESOURCE_MISC_BUFFER_STRUCTURED : D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
		}
		if (access_hint_ & EAH_DrawIndirectArgs)
		{
			misc_flags |= D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS;
		}
	}

	void D3D12GraphicsBuffer::DoResize()
	{
		BOOST_ASSERT(size_in_byte_ != 0);

		this->CreateBuffer(nullptr);
	}

	void D3D12GraphicsBuffer::CreateBuffer(D3D11_SUBRESOURCE_DATA const * subres_init)
	{
		D3D11_BUFFER_DESC desc;
		this->GetD3DFlags(desc.Usage, desc.CPUAccessFlags, desc.BindFlags, desc.MiscFlags);
		desc.ByteWidth = size_in_byte_;
		desc.StructureByteStride = NumFormatBytes(fmt_as_shader_res_);

		ID3D11Buffer* buffer;
		TIF(d3d_device_->CreateBuffer(&desc, subres_init, &buffer));
		buffer_ = MakeCOMPtr(buffer);

		if ((access_hint_ & EAH_GPU_Read) && (fmt_as_shader_res_ != EF_Unknown))
		{
			D3D11_SHADER_RESOURCE_VIEW_DESC sr_desc;
			sr_desc.Format = (access_hint_ & EAH_GPU_Structured) ? DXGI_FORMAT_UNKNOWN : D3D12Mapping::MappingFormat(fmt_as_shader_res_);
			sr_desc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
			sr_desc.Buffer.ElementOffset = 0;
			sr_desc.Buffer.ElementWidth = size_in_byte_ / desc.StructureByteStride;

			ID3D11ShaderResourceView* d3d_sr_view;
			TIF(d3d_device_->CreateShaderResourceView(buffer_.get(), &sr_desc, &d3d_sr_view));
			d3d_sr_view_ = MakeCOMPtr(d3d_sr_view);
		}

		if ((access_hint_ & EAH_GPU_Unordered) && (fmt_as_shader_res_ != EF_Unknown))
		{
			D3D11_UNORDERED_ACCESS_VIEW_DESC uav_desc;
			if (access_hint_ & EAH_Raw)
			{
				uav_desc.Format = DXGI_FORMAT_R32_TYPELESS;
			}
			else if (access_hint_ & EAH_GPU_Structured)
			{
				uav_desc.Format = DXGI_FORMAT_UNKNOWN;
			}
			else
			{
				uav_desc.Format = D3D12Mapping::MappingFormat(fmt_as_shader_res_);
			}
			uav_desc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
			uav_desc.Buffer.FirstElement = 0;
			uav_desc.Buffer.NumElements = size_in_byte_ / desc.StructureByteStride;
			uav_desc.Buffer.Flags = 0;
			if (access_hint_ & EAH_Raw)
			{
				uav_desc.Buffer.Flags |= D3D11_BUFFER_UAV_FLAG_RAW;
			}
			if (access_hint_ & EAH_Append)
			{
				uav_desc.Buffer.Flags |= D3D11_BUFFER_UAV_FLAG_APPEND;
			}
			if (access_hint_ & EAH_Counter)
			{
				uav_desc.Buffer.Flags |= D3D11_BUFFER_UAV_FLAG_COUNTER;
			}

			ID3D11UnorderedAccessView* d3d_ua_view;
			TIF(d3d_device_->CreateUnorderedAccessView(buffer_.get(), &uav_desc, &d3d_ua_view));
			d3d_ua_view_ = MakeCOMPtr(d3d_ua_view);
		}
	}

	void* D3D12GraphicsBuffer::Map(BufferAccess ba)
	{
		BOOST_ASSERT(buffer_);

		D3D11_MAP type;
		switch (ba)
		{
		case BA_Read_Only:
			type = D3D11_MAP_READ;
			break;

		case BA_Write_Only:
			if ((EAH_CPU_Write == access_hint_) || ((EAH_CPU_Write | EAH_GPU_Read) == access_hint_))
			{
				type = D3D11_MAP_WRITE_DISCARD;
			}
			else
			{
				type = D3D11_MAP_WRITE;
			}
			break;

		case BA_Read_Write:
			type = D3D11_MAP_READ_WRITE;
			break;

		case BA_Write_No_Overwrite:
			type = D3D11_MAP_WRITE_NO_OVERWRITE;
			break;

		default:
			BOOST_ASSERT(false);
			type = D3D11_MAP_READ;
			break;
		}

		D3D11_MAPPED_SUBRESOURCE mapped;
		TIF(d3d_imm_ctx_->Map(buffer_.get(), 0, type, 0, &mapped));
		return mapped.pData;
	}

	void D3D12GraphicsBuffer::Unmap()
	{
		BOOST_ASSERT(buffer_);

		d3d_imm_ctx_->Unmap(buffer_.get(), 0);
	}

	void D3D12GraphicsBuffer::CopyToBuffer(GraphicsBuffer& rhs)
	{
		BOOST_ASSERT(this->Size() <= rhs.Size());

		D3D12GraphicsBuffer& d3d_gb = *checked_cast<D3D12GraphicsBuffer*>(&rhs);
		if (this->Size() == rhs.Size())
		{
			d3d_imm_ctx_->CopyResource(d3d_gb.D3DBuffer().get(), buffer_.get());
		}
		else
		{
			D3D11_BOX box;
			box.left = 0;
			box.right = this->Size();
			box.top = 0;
			box.bottom = 1;
			box.front = 0;
			box.back = 1;
			d3d_imm_ctx_->CopySubresourceRegion(d3d_gb.D3DBuffer().get(), 0, 0, 0, 0, buffer_.get(), 0, &box);
		}
	}
}
