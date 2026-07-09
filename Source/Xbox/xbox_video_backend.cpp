// OG Xbox port: video backend registration.
// Replaces VideoBackendBase.cpp (which referenced the desktop D3D9/DX11/OGL
// plugins). The Xbox build has exactly one backend: the Software renderer,
// presented through the D3D8 blit path (xbox_d3d8 / SWRenderer_Xbox).

#include "VideoBackendBase.h"
#include "../../Plugins/Plugin_VideoSoftware/Src/VideoBackend.h"

std::vector<VideoBackend*> g_available_video_backends;
VideoBackend* g_video_backend = NULL;
static VideoBackend* s_default_backend = NULL;

void VideoBackend::PopulateList()
{
	g_available_video_backends.push_back(new SW::VideoSoftware);
	s_default_backend = g_video_backend = g_available_video_backends.front();
}

void VideoBackend::ClearList()
{
	while (!g_available_video_backends.empty())
	{
		delete g_available_video_backends.back();
		g_available_video_backends.pop_back();
	}
	g_video_backend = NULL;
	s_default_backend = NULL;
}

void VideoBackend::ActivateBackend(const std::string& name)
{
	if (name.length() == 0)
		g_video_backend = s_default_backend;
	for (std::vector<VideoBackend*>::const_iterator it = g_available_video_backends.begin();
	     it != g_available_video_backends.end(); ++it)
		if (name == (*it)->GetName())
			g_video_backend = *it;
}
