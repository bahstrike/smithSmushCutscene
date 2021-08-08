#include "smith.h"
#include "audioman.h"
#include "smushvideo.h"
#include "graphicsman.h"

extern "C"
{
	SMITHCALLS* smith = nullptr;

	int __cdecl SmithQueryPlugin(PLUGININFO& p)
	{
		strcpy(p.name, "Smush Cutscene");
		strcpy(p.author, "bahstrike");
		strcpy(p.authorEmail, "strike@bah.wtf");
		strcpy(p.attributions, "clone2727&Copyright 2012 Matthew Hoops (clone2727@gmail.com) author of \"smushplay - A simple LucasArts SMUSH video player\"");
		strcpy(p.desc, "Provides Smush cutscene support");
		strcpy(p.homepageURL, "https://github.com/bahstrike/smithSmushCutscene");
		p.smithRequiredVer = SMITHVERSION;
		p.ver = 100;
		p.purpose = PP_SMUSHCUTSCENE;

		//p.authoritykey

		return 1337;
	}

	int __cdecl InitializePlugin(SMITHCALLS* _smith)
	{
		smith = _smith;



		return true;
	}

	void __cdecl ShutdownPlugin()
	{


		smith = nullptr;
	}



	struct SMUSH
	{
		AudioManager* audio;
		SMUSHVideo* video;
		GraphicsManager* gfx;
	};

	SMUSH* __cdecl smushLoad(void* pBuffer, int len)
	{
		SMUSH* smush = new SMUSH;

		smush->audio = new AudioManager();
		smush->audio->init();

		smush->video = new SMUSHVideo(*smush->audio);
		smush->video->load(pBuffer, len);

		smush->gfx = new GraphicsManager();
		smush->gfx->init(smush->video->getWidth(), smush->video->getHeight(), smush->video->isHighColor());

		//smush->video->play(*smush->gfx);

		timeBeginPeriod(1);

		return smush;
	}

	void __cdecl smushGetInfo(SMUSH* smush, int& width, int& height, int& numframes, double& fps)
	{
		if (smush == nullptr)
			return;

		width = smush->video->getWidth();
		height = smush->video->getHeight();
		numframes = smush->video->getNumFrames();
		fps = smush->video->getFPS();
	}

	int __cdecl smushFrame(SMUSH* smush)
	{
		if (smush == nullptr)
			return 2;

		return smush->video->frame(*smush->gfx);
	}

	void __cdecl smushGetFrame(SMUSH* smush, void* scan0, int stride)
	{
		if (smush == nullptr)
			return;

		smush->gfx->toBitmap(scan0, stride);
	}

	void __cdecl smushGetAudio(SMUSH* smush, void* buffer, int len)
	{
		if (smush == nullptr)
			return;

		smush->audio->callbackHandler((byte*)buffer, len);
	}

	int __cdecl smushGetCutsceneStringId(SMUSH* smush)
	{
		return smush->video->getCutsceneStringId();
	}

	void __cdecl smushDestroy(SMUSH* smush)
	{
		if (smush == nullptr)
			return;

		timeEndPeriod(1);

		delete smush->gfx;
		delete smush->video;
		delete smush->audio;
		delete smush;
	}
}