// OGLESQuery.hpp
// KlayGE OpenGL ES遮挡检测类 实现文件
// Ver 3.0.0
// 版权所有(C) 龚敏敏, 2005
// Homepage: http://www.klayge.org
//
// 3.0.0
// 初次建立 (2005.9.27)
//
// 修改记录
//////////////////////////////////////////////////////////////////////////////////

#ifndef _OGLESQUERY_HPP
#define _OGLESQUERY_HPP

#pragma once

#include <KlayGE/Query.hpp>

namespace KlayGE
{
	class OGLESConditionalRender : public ConditionalRender
	{
	public:
		OGLESConditionalRender();
		~OGLESConditionalRender();

		void Begin();
		void End();

		void BeginConditionalRender();
		void EndConditionalRender();

		bool AnySamplesPassed();

	private:
		GLuint query_;

		static glGenQueriesFUNC glGenQueries_;
		static glDeleteQueriesFUNC glDeleteQueries_;
		static glBeginQueryFUNC glBeginQuery_;
		static glEndQueryFUNC glEndQuery_;
		static glGetQueryObjectuivFUNC glGetQueryObjectuiv_;
	};

	class OGLESTimerQuery : public TimerQuery
	{
	public:
		OGLESTimerQuery();
		~OGLESTimerQuery();

		void Begin();
		void End();

		double TimeElapsed();

	private:
		GLuint query_;

		static glGenQueriesFUNC glGenQueries_;
		static glDeleteQueriesFUNC glDeleteQueries_;
		static glBeginQueryFUNC glBeginQuery_;
		static glEndQueryFUNC glEndQuery_;
		static glGetQueryObjectuivFUNC glGetQueryObjectuiv_;
	};
}

#endif		// _OGLQUERY_HPP
