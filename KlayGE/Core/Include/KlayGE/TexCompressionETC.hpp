/**
* @file TexCompressionETC.hpp
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

#ifndef _TEXCOMPRESSIONETC_HPP
#define _TEXCOMPRESSIONETC_HPP

#pragma once

#include <KlayGE/TexCompression.hpp>

namespace KlayGE
{
#ifdef KLAYGE_HAS_STRUCT_PACK
	#pragma pack(push, 1)
#endif
	struct ETC1Block
	{
		uint8_t r;
		uint8_t g;
		uint8_t b;
		uint8_t cw_diff_flip;
		uint16_t msb;
		uint16_t lsb;
	};

	struct ETC2TModeBlock
	{
		uint8_t r1;
		uint8_t g1_b1;
		uint8_t r2_g2;
		uint8_t b2_d;
		uint16_t msb;
		uint16_t lsb;
	};

	struct ETC2HModeBlock
	{
		uint8_t r1_g1;
		uint8_t g1_b1;
		uint8_t b1_r2_g2;
		uint8_t g2_b2_d;
		uint16_t msb;
		uint16_t lsb;
	};

	struct ETC2PlanarModeBlock
	{
		uint8_t ro_go;
		uint8_t go_bo;
		uint8_t bo;
		uint8_t bo_rh;
		uint8_t gh_bh;
		uint8_t bh_rv;
		uint8_t rv_gv;
		uint8_t gv_bv;
	};

	union ETC2Block
	{
		ETC1Block etc1;
		ETC2TModeBlock etc2_t_mode;
		ETC2HModeBlock etc2_h_mode;
		ETC2PlanarModeBlock etc2_planar_mode;
	};
#ifdef KLAYGE_HAS_STRUCT_PACK
	#pragma pack(pop)
#endif

	class KLAYGE_CORE_API TexCompressionETC1 : public TexCompression
	{
	public:
		TexCompressionETC1();

		virtual void EncodeBlock(void* output, void const * input, TexCompressionMethod method) KLAYGE_OVERRIDE;
		virtual void DecodeBlock(void* output, void const * input) KLAYGE_OVERRIDE;

		void DecodeETCIndividualModeInternal(uint32_t* argb, ETC1Block const & etc1);
		void DecodeETCDifferentialModeInternal(uint32_t* argb, ETC1Block const & etc1, bool alpha);
	};

	class KLAYGE_CORE_API TexCompressionETC2RGB8 : public TexCompression
	{
	public:
		TexCompressionETC2RGB8();

		virtual void EncodeBlock(void* output, void const * input, TexCompressionMethod method) KLAYGE_OVERRIDE;
		virtual void DecodeBlock(void* output, void const * input) KLAYGE_OVERRIDE;

		void DecodeETCTModeInternal(uint32_t* argb, ETC2TModeBlock const & etc2, bool alpha);
		void DecodeETCHModeInternal(uint32_t* argb, ETC2HModeBlock const & etc2, bool alpha);
		void DecodeETCPlanarModeInternal(uint32_t* argb, ETC2PlanarModeBlock const & etc2);

	private:
		TexCompressionETC1Ptr etc1_codec_;
	};

	class KLAYGE_CORE_API TexCompressionETC2RGB8A1 : public TexCompression
	{
	public:
		TexCompressionETC2RGB8A1();

		virtual void EncodeBlock(void* output, void const * input, TexCompressionMethod method) KLAYGE_OVERRIDE;
		virtual void DecodeBlock(void* output, void const * input) KLAYGE_OVERRIDE;

	private:
		TexCompressionETC1Ptr etc1_codec_;
		TexCompressionETC2RGB8Ptr etc2_rgb8_codec_;
	};
}

#endif		// _TEXCOMPRESSIONETC_HPP
