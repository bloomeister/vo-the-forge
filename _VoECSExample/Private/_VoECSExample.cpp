/*
 * Copyright (c) 2017-2024 The Forge Interactive Inc.
 *
 * This file is part of The-Forge
 * (see https://github.com/ConfettiFX/The-Forge).
 *
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

 // ECS
#include "Game/ThirdParty/OpenSource/flecs/flecs.h"

// Interfaces
#include "Application/Interfaces/IApp.h"
#include "Application/Interfaces/IFont.h"
#include "OS/Interfaces/IInput.h"
#include "Application/Interfaces/IProfiler.h"
#include "Application/Interfaces/IScreenshot.h"
#include "Application/Interfaces/IUI.h"
#include "Game/Interfaces/IScripting.h"
#include "Utilities/Interfaces/IFileSystem.h"
#include "Utilities/Interfaces/ILog.h"
#include "Utilities/Interfaces/IThread.h"
#include "Utilities/Interfaces/ITime.h"

#include "Graphics/FSL/defaults.h"
#include "Shaders/FSL/Global.srt.h"

// Renderer
#include "Graphics/Interfaces/IGraphics.h"
#include "Graphics/Interfaces/IRay.h"
#include "Resources/ResourceLoader/Interfaces/IResourceLoader.h"

#include "Utilities/RingBuffer.h"

// Math
#include "Utilities/Math/MathTypes.h"

#include "Utilities/Interfaces/IMemory.h" // Must be the last include in a cpp file

struct SpriteData
{
	float posX, posY;
	float scale;
	float pad;
	float colR, colG, colB;
	float sprite;
};

// COMPONENTS
struct WorldBoundsComponent
{
	float xMin, xMax, yMin, yMax;
};

struct PositionComponent
{
	float x, y;
};

struct SpriteComponent
{
	float colorR, colorG, colorB;
	int   spriteIndex;
	float scale;
};

struct MoveComponent
{
	float velx, vely;
};

ECS_COMPONENT_DECLARE(WorldBoundsComponent);
ECS_COMPONENT_DECLARE(PositionComponent);
ECS_COMPONENT_DECLARE(SpriteComponent);
ECS_COMPONENT_DECLARE(MoveComponent);

// #NOTE: Two sets of resources (one in flight and one being used on CPU)
const uint32_t gDataBufferCount = 2;

ProfileToken gGpuProfileToken;

Renderer* pRenderer = NULL;

Queue* pGraphicsQueue = NULL;
GpuCmdRing gGraphicsCmdRing = {};

SwapChain* pSwapChain = NULL;
Semaphore* pImageAcquiredSemaphore = NULL;

Shader* pSpriteShader = NULL;
Buffer* pSpriteVertexBuffers[gDataBufferCount] = { NULL };
Buffer* pSpriteIndexBuffer = NULL;
Buffer* pSpriteVertexBuffer = NULL;
Pipeline* pSpritePipeline = NULL;

DescriptorSet* pDescriptorSetTexture = NULL;
DescriptorSet* pDescriptorSetUniforms = NULL;
Sampler* pLinearClampSampler = NULL;

Texture* pSpriteTexture = NULL;

uint32_t gFrameIndex = 0;

uint32_t gDrawSpriteCount = 0;
uint32_t gAvailableCores = 1;

ecs_world_t* gECSWorld = NULL;

ecs_query_t* gECSSpriteQuery = NULL;
ecs_query_t* gECSAvoidQuery = NULL;

// Based on: https://github.com/aras-p/dod-playground

#if defined(__ANDROID__)
const uint32_t gSpriteEntityCount = 108;
const uint32_t gAvoidEntityCount = 20;
#else
const uint32_t gSpriteEntityCount = 50000;
const uint32_t gAvoidEntityCount = 100;
#endif
const uint32_t gMaxSpriteCount = gAvoidEntityCount + gSpriteEntityCount;

SpriteData gSpriteData[gMaxSpriteCount] = {};

static bool gMultiThread = true;

UIComponent* pGUIWindow = nullptr;

uint32_t gFontID = 0;

MoveComponent createMoveComponent(float minSpeed, float maxSpeed)
{
	MoveComponent move;

	// random angle
	float angle = randomFloat01() * 3.1415926f * 2;
	// random movement speed between given min & max
	float speed = randomFloat(minSpeed, maxSpeed);
	// velocity x & y components
	move.velx = cosf(angle) * speed;
	move.vely = sinf(angle) * speed;

	return move;
}

struct AvoidComponent
{
	float distanceSq;
};
ECS_COMPONENT_DECLARE(AvoidComponent);

void MoveSystem(ecs_iter_t* it)
{
	PositionComponent* positions = ecs_field(it, PositionComponent, 0);
	MoveComponent* moves = ecs_field(it, MoveComponent, 1);

	const WorldBoundsComponent* bounds = ecs_singleton_get(it->world, WorldBoundsComponent);

	for (int i = 0; i < it->count; i++)
	{
		PositionComponent& pos = positions[i];
		MoveComponent& move = moves[i];

		// update position based on movement velocity & delta time
		pos.x += move.velx * it->delta_time;
		pos.y += move.vely * it->delta_time;

		// check against world bounds; put back onto bounds and mirror the velocity component to "bounce" back
		if (pos.x < bounds->xMin)
		{
			move.velx = -move.velx;
			pos.x = bounds->xMin;
		}
		if (pos.x > bounds->xMax)
		{
			move.velx = -move.velx;
			pos.x = bounds->xMax;
		}
		if (pos.y < bounds->yMin)
		{
			move.vely = -move.vely;
			pos.y = bounds->yMin;
		}
		if (pos.y > bounds->yMax)
		{
			move.vely = -move.vely;
			pos.y = bounds->yMax;
		}
	}
}

static float DistanceSq(PositionComponent a, PositionComponent b)
{
	float dx = a.x - b.x;
	float dy = a.y - b.y;
	return dx * dx + dy * dy;
}

void AvoidanceSystem(ecs_iter_t* it)
{
	PositionComponent* positions = ecs_field(it, PositionComponent, 0);
	MoveComponent* moves = ecs_field(it, MoveComponent, 1);
	SpriteComponent* sprites = ecs_field(it, SpriteComponent, 2);

	for (int i = 0; i < it->count; i++)
	{
		PositionComponent& pos = positions[i];
		MoveComponent& move = moves[i];
		SpriteComponent& sprite = sprites[i];

		ecs_iter_t avoidIter = ecs_query_iter(it->world, gECSAvoidQuery);
		while (ecs_query_next(&avoidIter))
		{
			PositionComponent* avoidPositions = ecs_field(&avoidIter, PositionComponent, 0);
			SpriteComponent* avoidSprites = ecs_field(&avoidIter, SpriteComponent, 2);
			AvoidComponent* avoidDistances = ecs_field(&avoidIter, AvoidComponent, 2);

			for (int j = 0; j < avoidIter.count; j++)
			{
				const PositionComponent& avoidPosition = avoidPositions[j];
				const SpriteComponent& avoidSprite = avoidSprites[j];
				const AvoidComponent& avoidDistance = avoidDistances[j];

				if (DistanceSq(pos, avoidPosition) < avoidDistance.distanceSq)
				{
					// flip velocity
					move.velx = -move.velx;
					move.vely = -move.vely;

					// move us out of collision, by moving just a tiny bit more than we'd normally move during a frame
					pos.x += move.velx * it->delta_time * 1.1f;
					pos.y += move.vely * it->delta_time * 1.1f;

					sprite.colorR = avoidSprite.colorR;
					sprite.colorG = avoidSprite.colorG;
					sprite.colorB = avoidSprite.colorB;
				}
			}
		}
	}
}

struct CreationData
{
	WorldBoundsComponent* bounds;
	const char* entityTypeName;
};

static void createEntities(void* pData)
{
	CreationData data = *(CreationData*)pData;

	ecs_entity_t entityId = ecs_new(gECSWorld);

	float x = randomFloat(data.bounds->xMin, data.bounds->xMax);
	float y = randomFloat(data.bounds->yMin, data.bounds->yMax);

	PositionComponent position = { x, y };
	MoveComponent     move = createMoveComponent(10.0f, 20.0f);
	SpriteComponent   sprite = {};

	if (!strcmp(data.entityTypeName, "avoid"))
	{
		AvoidComponent avoid = { 1.3f * 1.3f };
		ecs_set(gECSWorld, entityId, AvoidComponent, avoid);

		position.x *= 0.2f;
		position.y *= 0.2f;
		sprite.colorR = randomFloat(0.5f, 1.0f);
		sprite.colorG = randomFloat(0.5f, 1.0f);
		sprite.colorB = randomFloat(0.5f, 1.0f);
		sprite.scale = 1.0f;
		sprite.spriteIndex = 5;
	}
	else
	{
		sprite.colorR = 1.0f;
		sprite.colorG = 1.0f;
		sprite.colorB = 1.0f;
		sprite.scale = 0.5f;
		sprite.spriteIndex = randomInt(0, 5);
	}

	ecs_set(gECSWorld, entityId, PositionComponent, position);
	ecs_set(gECSWorld, entityId, MoveComponent, move);
	ecs_set(gECSWorld, entityId, SpriteComponent, sprite);
}

class EntityComponentSystem : public IApp
{
public:
	bool Init()
	{
		// FILE PATHS
		// Align resource dirs with PathStatement to ensure assets are found in Art/ and build output.
		/*fsSetPathForResourceDir(pSystemFileIO, RD_SHADER_BINARIES, "CompiledShaders/");
		fsSetPathForResourceDir(pSystemFileIO, RD_TEXTURES, "../../../Art/Textures/dds/");
		fsSetPathForResourceDir(pSystemFileIO, RD_FONTS, "../../../Art/Fonts/");
		fsSetPathForResourceDir(pSystemFileIO, RD_SCRIPTS, "Scripts/");
		fsSetPathForResourceDir(pSystemFileIO, RD_SCREENSHOTS, "Screenshots/");
		fsSetPathForResourceDir(pSystemFileIO, RD_DEBUG, "Debug/");*/

		RendererDesc settings;
		memset(&settings, 0, sizeof(settings));
		initGPUConfiguration(settings.pExtendedSettings);
		initRenderer(GetName(), &settings, &pRenderer);
		// check for init success
		if (!pRenderer) {
			return false;
		}
		setupGPUConfigurationPlatformParameters(pRenderer, settings.pExtendedSettings);

		QueueDesc queueDesc = {};
		queueDesc.mType = QUEUE_TYPE_GRAPHICS;
		queueDesc.mFlag = QUEUE_FLAG_INIT_MICROPROFILE;
		initQueue(pRenderer, &queueDesc, &pGraphicsQueue);

		GpuCmdRingDesc cmdRingDesc = {};
		cmdRingDesc.pQueue = pGraphicsQueue;
		cmdRingDesc.mPoolCount = gDataBufferCount;
		cmdRingDesc.mCmdPerPoolCount = 1;
		cmdRingDesc.mAddSyncPrimitives = true;
		initGpuCmdRing(pRenderer, &cmdRingDesc, &gGraphicsCmdRing);

		initSemaphore(pRenderer, &pImageAcquiredSemaphore);

		{
			RootSignatureDesc rootDesc = {};
			INIT_RS_DESC(rootDesc, "default.rootsig", "compute.rootsig");
			initRootSignature(pRenderer, &rootDesc);
		}

		initResourceLoaderInterface(pRenderer);

		// Load fonts
		FontDesc font = {};
		font.pFontPath = "TitilliumText/TitilliumText-Bold.otf";
		fntDefineFonts(&font, 1, &gFontID);

		FontSystemDesc fontRenderDesc = {};
		fontRenderDesc.pRenderer = pRenderer;
		if (!initFontSystem(&fontRenderDesc))
			return false; // report?

		// Initialize Forge User Interface Rendering
		UserInterfaceDesc uiRenderDesc = {};
		uiRenderDesc.pRenderer = pRenderer;
		initUserInterface(&uiRenderDesc);

		// Initialize micro profiler and its UI.
		ProfilerDesc profiler = {};
		profiler.pRenderer = pRenderer;
		initProfiler(&profiler);

		gGpuProfileToken = initGpuProfiler(pRenderer, pGraphicsQueue, "Graphics");

		SamplerDesc samplerDesc = { FILTER_LINEAR,
									FILTER_LINEAR,
									MIPMAP_MODE_LINEAR,
									ADDRESS_MODE_CLAMP_TO_EDGE,
									ADDRESS_MODE_CLAMP_TO_EDGE,
									ADDRESS_MODE_CLAMP_TO_EDGE };
		addSampler(pRenderer, &samplerDesc, &pLinearClampSampler);

		// Instance buffer
		BufferLoadDesc spriteVbDesc = {};
		spriteVbDesc.mDesc.mDescriptors = DESCRIPTOR_TYPE_BUFFER;
		spriteVbDesc.mDesc.mMemoryUsage = RESOURCE_MEMORY_USAGE_GPU_ONLY;
		spriteVbDesc.mDesc.mFlags = BUFFER_CREATION_FLAG_NONE;
		spriteVbDesc.mDesc.mStartState = RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
		spriteVbDesc.mDesc.mFirstElement = 0;
		spriteVbDesc.mDesc.mElementCount = gMaxSpriteCount;
		spriteVbDesc.mDesc.mStructStride = sizeof(SpriteData);
		spriteVbDesc.mDesc.mSize = gMaxSpriteCount * spriteVbDesc.mDesc.mStructStride;
		spriteVbDesc.pData = gSpriteData;
		for (uint32_t i = 0; i < gDataBufferCount; ++i)
		{
			spriteVbDesc.ppBuffer = &pSpriteVertexBuffers[i];
			addResource(&spriteVbDesc, NULL);
		}

		// Index buffer
		uint16_t indices[] = {
			0, 1, 2, 2, 1, 3,
		};
		BufferLoadDesc spriteIBDesc = {};
		spriteIBDesc.mDesc.mDescriptors = DESCRIPTOR_TYPE_INDEX_BUFFER;
		spriteIBDesc.mDesc.mMemoryUsage = RESOURCE_MEMORY_USAGE_GPU_ONLY;
		spriteIBDesc.mDesc.mSize = sizeof(indices);
		spriteIBDesc.pData = indices;
		spriteIBDesc.ppBuffer = &pSpriteIndexBuffer;
		addResource(&spriteIBDesc, NULL);

		// Vertex buffer
		float vertices[] = {
			0,
			1.0,
			2.0,
			3.0,
		};
		BufferLoadDesc spriteVBDesc = {};
		spriteVBDesc.mDesc.mDescriptors = DESCRIPTOR_TYPE_VERTEX_BUFFER;
		spriteVBDesc.mDesc.mMemoryUsage = RESOURCE_MEMORY_USAGE_GPU_ONLY;
		spriteVBDesc.mDesc.mSize = sizeof(vertices);
		spriteVBDesc.pData = vertices;
		spriteVBDesc.ppBuffer = &pSpriteVertexBuffer;
		addResource(&spriteVBDesc, NULL);

		// Sprites texture
		TextureLoadDesc textureDesc = {};
		textureDesc.ppTexture = &pSpriteTexture;
		// Textures representing color should be stored in SRGB or HDR format
		textureDesc.mCreationFlag = TEXTURE_CREATION_FLAG_SRGB;
		textureDesc.pFileName = "sprites.tex";
		addResource(&textureDesc, NULL);

		/************************************************************************/
		// GUI
		/************************************************************************/
		UIComponentDesc guiDesc = {};
		guiDesc.mStartPosition = vec2(mSettings.mWidth * 0.01f, mSettings.mHeight * 0.1f);
		uiAddComponent("MT", &guiDesc, &pGUIWindow);

		CheckboxWidget Checkbox;
		Checkbox.pData = &gMultiThread;
		luaRegisterWidget(uiAddComponentWidget(pGUIWindow, "Threading", &Checkbox, WIDGET_TYPE_CHECKBOX));

		initEntityComponentSystem();
		ecs_log_set_level(0);

		gECSWorld = ecs_init();
		gAvailableCores = getNumCPUCores();
		// Set threads before creating entities to make sure we implemented properly the atomic operations from TheForge in Flecs.
		ecs_set_threads(gECSWorld, gMultiThread ? gAvailableCores : 1);

		ECS_COMPONENT_DEFINE(gECSWorld, SpriteComponent);
		ECS_COMPONENT_DEFINE(gECSWorld, MoveComponent);
		ECS_COMPONENT_DEFINE(gECSWorld, PositionComponent);
		ECS_COMPONENT_DEFINE(gECSWorld, WorldBoundsComponent);

		ECS_COMPONENT_DEFINE(gECSWorld, AvoidComponent);

		ecs_system_desc_t moveSystemDesc = {};
		moveSystemDesc.callback = MoveSystem;
		{
			ecs_entity_desc_t entDesc = {};
			entDesc.name = "MoveSystem";
			ecs_id_t adds[] = { EcsOnUpdate, 0 };
			entDesc.add = adds;
			moveSystemDesc.entity = ecs_entity_init(gECSWorld, &entDesc);
		}
		moveSystemDesc.query.terms[0].id = ecs_id(PositionComponent);
		moveSystemDesc.query.terms[0].inout = EcsInOut;
		moveSystemDesc.query.terms[1].id = ecs_id(MoveComponent);
		moveSystemDesc.query.terms[1].inout = EcsInOut;
		moveSystemDesc.multi_threaded = false;
		ecs_system_init(gECSWorld, &moveSystemDesc);

		ecs_system_desc_t avoidanceSystemDesc = {};
		avoidanceSystemDesc.callback = AvoidanceSystem;
		{
			ecs_entity_desc_t entDesc = {};
			entDesc.name = "AvoidanceSystem";
			ecs_id_t adds[] = { EcsPostUpdate, 0 };
			entDesc.add = adds;
			avoidanceSystemDesc.entity = ecs_entity_init(gECSWorld, &entDesc);
		}
		avoidanceSystemDesc.query.terms[0].id = ecs_id(PositionComponent);
		avoidanceSystemDesc.query.terms[0].inout = EcsInOut;
		avoidanceSystemDesc.query.terms[1].id = ecs_id(MoveComponent);
		avoidanceSystemDesc.query.terms[1].inout = EcsInOut;
		avoidanceSystemDesc.query.terms[2].id = ecs_id(SpriteComponent);
		avoidanceSystemDesc.query.terms[2].inout = EcsOut;
		avoidanceSystemDesc.query.terms[3].id = ecs_id(AvoidComponent);
		avoidanceSystemDesc.query.terms[3].inout = EcsIn;
		avoidanceSystemDesc.query.terms[3].oper = EcsNot;
		avoidanceSystemDesc.multi_threaded = true;
		ecs_system_init(gECSWorld, &avoidanceSystemDesc);

		ecs_query_desc_t spriteQuery = {};
		spriteQuery.terms[0].id = ecs_id(PositionComponent);
		spriteQuery.terms[1].id = ecs_id(MoveComponent);
		spriteQuery.terms[2].id = ecs_id(SpriteComponent);
		spriteQuery.terms[3].id = ecs_id(AvoidComponent);
		spriteQuery.terms[3].oper = EcsNot;
		gECSSpriteQuery = ecs_query_init(gECSWorld, &spriteQuery);

		ecs_query_desc_t avoidQuery = spriteQuery;
		avoidQuery.terms[3].oper = EcsAnd;
		gECSAvoidQuery = ecs_query_init(gECSWorld, &avoidQuery);

		ecs_singleton_ensure(gECSWorld, WorldBoundsComponent);
		WorldBoundsComponent* bounds = ecs_get_mut(gECSWorld, ecs_id(WorldBoundsComponent), WorldBoundsComponent);
		ASSERT(bounds);
		bounds->xMin = -80.0f;
		bounds->xMax = 80.0f;
		bounds->yMin = -50.0f;
		bounds->yMax = 50.0f;
		ecs_singleton_modified(gECSWorld, WorldBoundsComponent);

		CreationData data = { bounds, "sprite" };
		CreationData avoidData = { bounds, "avoid" };

		for (size_t i = 0; i < gSpriteEntityCount; ++i)
		{
			createEntities(&data);
		}

		for (size_t i = 0; i < gAvoidEntityCount; ++i)
		{
			createEntities(&avoidData);
		}

		AddCustomInputBindings();

		gFrameIndex = 0;
		waitForAllResourceLoads();
		if (pSpriteTexture)
			LOGF(LogLevel::eINFO, "Loaded sprite texture '%s'", "sprites.tex");
		else
			LOGF(LogLevel::eERROR, "Failed to load sprite texture '%s'", "sprites.tex");

		return true;
	}

	void Exit()
	{
		ecs_query_fini(gECSAvoidQuery);
		ecs_query_fini(gECSSpriteQuery);
		ecs_fini(gECSWorld);

		exitProfiler();

		exitUserInterface();

		exitFontSystem();

		for (uint32_t i = 0; i < gDataBufferCount; ++i)
		{
			removeResource(pSpriteVertexBuffers[i]);
		}
		removeResource(pSpriteTexture);
		removeResource(pSpriteVertexBuffer);
		removeResource(pSpriteIndexBuffer);

		removeSampler(pRenderer, pLinearClampSampler);

		exitSemaphore(pRenderer, pImageAcquiredSemaphore);
		exitGpuCmdRing(pRenderer, &gGraphicsCmdRing);

		exitResourceLoaderInterface(pRenderer);
		exitRootSignature(pRenderer);
		exitQueue(pRenderer, pGraphicsQueue);
		exitRenderer(pRenderer);
		pRenderer = NULL;

		exitGPUConfiguration();
	}

	bool Load(ReloadDesc* pReloadDesc)
	{
		if (pReloadDesc->mType & RELOAD_TYPE_SHADER)
		{
			addShaders();
			addDescriptorSets();
		}

		if (pReloadDesc->mType & (RELOAD_TYPE_RESIZE | RELOAD_TYPE_RENDERTARGET))
		{
			if (!addSwapChain())
				return false;
		}

		if (pReloadDesc->mType & (RELOAD_TYPE_SHADER | RELOAD_TYPE_RENDERTARGET))
		{
			addPipelines();
		}

		loadProfilerUI(mSettings.mWidth, mSettings.mHeight);
		prepareDescriptorSets();

		UserInterfaceLoadDesc uiLoad = {};
		uiLoad.mColorFormat = pSwapChain->ppRenderTargets[0]->mFormat;
		uiLoad.mHeight = mSettings.mHeight;
		uiLoad.mWidth = mSettings.mWidth;
		uiLoad.mLoadType = pReloadDesc->mType;
		loadUserInterface(&uiLoad);

		FontSystemLoadDesc fontLoad = {};
		fontLoad.mColorFormat = pSwapChain->ppRenderTargets[0]->mFormat;
		fontLoad.mHeight = mSettings.mHeight;
		fontLoad.mWidth = mSettings.mWidth;
		fontLoad.mLoadType = pReloadDesc->mType;
		loadFontSystem(&fontLoad);

		initScreenshotCapturer(pRenderer, pGraphicsQueue, GetName());

		return true;
	}

	void Unload(ReloadDesc* pReloadDesc)
	{
		waitQueueIdle(pGraphicsQueue);

		unloadProfilerUI();
		unloadFontSystem(pReloadDesc->mType);
		unloadUserInterface(pReloadDesc->mType);

		if (pReloadDesc->mType & (RELOAD_TYPE_SHADER | RELOAD_TYPE_RENDERTARGET))
		{
			removePipelines();
		}

		if (pReloadDesc->mType & (RELOAD_TYPE_RESIZE | RELOAD_TYPE_RENDERTARGET))
		{
			removeSwapChain(pRenderer, pSwapChain);
		}

		if (pReloadDesc->mType & RELOAD_TYPE_SHADER)
		{
			removeDescriptorSets();
			removeShaders();
		}

		exitScreenshotCapturer();
	}

	void Update(float deltaTime)
	{
		static bool oldMultiThread = gMultiThread;
		if (oldMultiThread != gMultiThread)
		{
			oldMultiThread = gMultiThread;
			ecs_set_threads(gECSWorld, gMultiThread ? gAvailableCores : 1);
		}

		// Scene Update
		ecs_progress(gECSWorld, deltaTime * 3.0f);

		// Iterate all entities with transform and plane component
		gDrawSpriteCount = 0;
		float globalScale = 0.05f;

		ecs_iter_t spriteIter = ecs_query_iter(gECSWorld, gECSSpriteQuery);
		while (ecs_query_next(&spriteIter))
		{
			PositionComponent* positions = ecs_field(&spriteIter, PositionComponent, 0);
			SpriteComponent* sprites = ecs_field(&spriteIter, SpriteComponent, 2);
			for (int i = 0; i < spriteIter.count; i++)
			{
				const PositionComponent& position = positions[i];
				const SpriteComponent& sprite = sprites[i];
				SpriteData& spriteData = gSpriteData[gDrawSpriteCount++];
				spriteData.posX = position.x * globalScale;
				spriteData.posY = position.y * globalScale;
				spriteData.scale = sprite.scale * globalScale;
				spriteData.colR = sprite.colorR;
				spriteData.colG = sprite.colorG;
				spriteData.colB = sprite.colorB;
				spriteData.sprite = (float)sprite.spriteIndex;
			}
		}

		ecs_iter_t avoidIter = ecs_query_iter(gECSWorld, gECSAvoidQuery);
		while (ecs_query_next(&avoidIter))
		{
			PositionComponent* positions = ecs_field(&avoidIter, PositionComponent, 0);
			SpriteComponent* sprites = ecs_field(&avoidIter, SpriteComponent, 2);
			for (int i = 0; i < avoidIter.count; i++)
			{
				const PositionComponent& position = positions[i];
				const SpriteComponent& sprite = sprites[i];

				SpriteData& spriteData = gSpriteData[gDrawSpriteCount++];
				spriteData.posX = position.x * globalScale;
				spriteData.posY = position.y * globalScale;
				spriteData.scale = sprite.scale * globalScale;
				spriteData.colR = sprite.colorR;
				spriteData.colG = sprite.colorG;
				spriteData.colB = sprite.colorB;
				spriteData.sprite = (float)sprite.spriteIndex;
			}
		}
	}

	void Draw()
	{
		const bool swapVsyncEnabled = pSwapChain->mEnableVsync != 0;
		if (swapVsyncEnabled != mSettings.mVSyncEnabled)
		{
			waitQueueIdle(pGraphicsQueue);
			::toggleVSync(pRenderer, &pSwapChain);
		}

		if (inputGetValue(0, CUSTOM_TOGGLE_FULLSCREEN))
		{
			toggleFullscreen(pWindow);
		}
		if (inputGetValue(0, CUSTOM_DUMP_PROFILE))
		{
			dumpProfileData(GetName());
		}
		if (inputGetValue(0, CUSTOM_EXIT))
		{
			requestShutdown();
		}

		uint32_t swapchainImageIndex;
		acquireNextImage(pRenderer, pSwapChain, pImageAcquiredSemaphore, NULL, &swapchainImageIndex);

		// Update vertex buffer
		ASSERT(gDrawSpriteCount >= 0 && gDrawSpriteCount <= gMaxSpriteCount);
		BufferUpdateDesc vboUpdateDesc = { pSpriteVertexBuffers[gFrameIndex] };
		vboUpdateDesc.mCurrentState = RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
		beginUpdateResource(&vboUpdateDesc);
		memcpy(vboUpdateDesc.pMappedData, gSpriteData, gDrawSpriteCount * sizeof(SpriteData));
		endUpdateResource(&vboUpdateDesc);

		// Stall if CPU is running "gDataBufferCount" frames ahead of GPU
		GpuCmdRingElement elem = getNextGpuCmdRingElement(&gGraphicsCmdRing, true, 1);
		FenceStatus       fenceStatus;
		getFenceStatus(pRenderer, elem.pFence, &fenceStatus);
		if (fenceStatus == FENCE_STATUS_INCOMPLETE)
		{
			waitForFences(pRenderer, 1, &elem.pFence);
		}

		resetCmdPool(pRenderer, elem.pCmdPool);

		RenderTarget* pRenderTarget = pSwapChain->ppRenderTargets[swapchainImageIndex];

		// simply record the screen cleaning command
		Cmd* cmd = elem.pCmds[0];
		beginCmd(cmd);
		cmdBeginGpuFrameProfile(cmd, gGpuProfileToken);

		RenderTargetBarrier barriers[] = {
			{ pRenderTarget, RESOURCE_STATE_PRESENT, RESOURCE_STATE_RENDER_TARGET },
		};
		cmdResourceBarrier(cmd, 0, NULL, 0, NULL, 1, barriers);

		BindRenderTargetsDesc bindRenderTargets = {};
		bindRenderTargets.mRenderTargetCount = 1;
		bindRenderTargets.mRenderTargets[0] = { pRenderTarget, LOAD_ACTION_CLEAR };
		cmdBindRenderTargets(cmd, &bindRenderTargets);
		cmdSetViewport(cmd, 0.0f, 0.0f, (float)pRenderTarget->mWidth, (float)pRenderTarget->mHeight, 0.0f, 1.0f);
		cmdSetScissor(cmd, 0, 0, pRenderTarget->mWidth, pRenderTarget->mHeight);

		// Draw Sprites
		if (gDrawSpriteCount > 0)
		{
			cmdBeginDebugMarker(cmd, 1, 0, 1, "Draw Sprites");
			cmdBindPipeline(cmd, pSpritePipeline);
			cmdBindDescriptorSet(cmd, 0, pDescriptorSetTexture);
			cmdBindDescriptorSet(cmd, gFrameIndex, pDescriptorSetUniforms);
			uint32_t vertexStride = sizeof(float);
			cmdBindVertexBuffer(cmd, 1, &pSpriteVertexBuffer, &vertexStride, NULL);
			cmdBindIndexBuffer(cmd, pSpriteIndexBuffer, INDEX_TYPE_UINT16, 0);
			cmdDrawIndexedInstanced(cmd, 6, 0, gDrawSpriteCount, 0, 0);
			cmdEndDebugMarker(cmd);
		}

		cmdBeginDebugMarker(cmd, 0, 1, 0, "Draw UI");

		FontDrawDesc uiTextDesc; // default
		uiTextDesc.mFontColor = 0xff00cc00;
		uiTextDesc.mFontSize = 18;
		uiTextDesc.mFontID = gFontID;
		float2 txtSize = cmdDrawCpuProfile(cmd, float2(8.0f, 15.0f), &uiTextDesc);
		cmdDrawGpuProfile(cmd, float2(8.0f, txtSize.y + 75.f), gGpuProfileToken, &uiTextDesc);

		cmdDrawUserInterface(cmd);
		cmdBindRenderTargets(cmd, NULL);
		cmdEndDebugMarker(cmd);

		barriers[0] = { pRenderTarget, RESOURCE_STATE_RENDER_TARGET, RESOURCE_STATE_PRESENT };
		cmdResourceBarrier(cmd, 0, NULL, 0, NULL, 1, barriers);

		cmdEndGpuFrameProfile(cmd, gGpuProfileToken);
		endCmd(cmd);

		FlushResourceUpdateDesc flushUpdateDesc = {};
		flushUpdateDesc.mNodeIndex = 0;
		flushResourceUpdates(&flushUpdateDesc);
		Semaphore* waitSemaphores[2] = { flushUpdateDesc.pOutSubmittedSemaphore, pImageAcquiredSemaphore };

		QueueSubmitDesc submitDesc = {};
		submitDesc.mCmdCount = 1;
		submitDesc.mSignalSemaphoreCount = 1;
		submitDesc.mWaitSemaphoreCount = TF_ARRAY_COUNT(waitSemaphores);
		submitDesc.ppCmds = &cmd;
		submitDesc.ppSignalSemaphores = &elem.pSemaphore;
		submitDesc.ppWaitSemaphores = waitSemaphores;
		submitDesc.pSignalFence = elem.pFence;
		queueSubmit(pGraphicsQueue, &submitDesc);
		QueuePresentDesc presentDesc = {};
		presentDesc.mIndex = (uint8_t)swapchainImageIndex;
		presentDesc.mWaitSemaphoreCount = 1;
		presentDesc.ppWaitSemaphores = &elem.pSemaphore;
		presentDesc.pSwapChain = pSwapChain;
		presentDesc.mSubmitDone = true;
		queuePresent(pGraphicsQueue, &presentDesc);
		flipProfiler();

		gFrameIndex = (gFrameIndex + 1) % gDataBufferCount;
	}

	const char* GetName() { return "_VoECSExample"; }

	bool addSwapChain()
	{
		SwapChainDesc swapChainDesc = {};
		swapChainDesc.mWindowHandle = pWindow->handle;
		swapChainDesc.mPresentQueueCount = 1;
		swapChainDesc.ppPresentQueues = &pGraphicsQueue;
		swapChainDesc.mWidth = mSettings.mWidth;
		swapChainDesc.mHeight = mSettings.mHeight;
		swapChainDesc.mImageCount = getRecommendedSwapchainImageCount(pRenderer, &pWindow->handle);
		swapChainDesc.mColorFormat = getSupportedSwapchainFormat(pRenderer, &swapChainDesc, COLOR_SPACE_SDR_SRGB);
		swapChainDesc.mColorSpace = COLOR_SPACE_SDR_SRGB;
		swapChainDesc.mColorClearValue = { { 0.02f, 0.02f, 0.02f, 1.0f } };
		swapChainDesc.mEnableVsync = mSettings.mVSyncEnabled;
		::addSwapChain(pRenderer, &swapChainDesc, &pSwapChain);

		return pSwapChain != NULL;
	}

	void addDescriptorSets()
	{
		DescriptorSetDesc setDescPersistent = SRT_SET_DESC(SrtData, Persistent, 1, 0);
		addDescriptorSet(pRenderer, &setDescPersistent, &pDescriptorSetTexture);
		DescriptorSetDesc setDescPerFrame = SRT_SET_DESC(SrtData, PerFrame, gDataBufferCount, 0);
		addDescriptorSet(pRenderer, &setDescPerFrame, &pDescriptorSetUniforms);
	}

	void removeDescriptorSets()
	{
		removeDescriptorSet(pRenderer, pDescriptorSetTexture);
		removeDescriptorSet(pRenderer, pDescriptorSetUniforms);
	}

	void addShaders()
	{
		// TODO: rename to sprite
		ShaderLoadDesc spriteShader = {};
		spriteShader.mVert.pFileName = "basic.vert";
		spriteShader.mFrag.pFileName = "basic.frag";

		addShader(pRenderer, &spriteShader, &pSpriteShader);
	}

	void removeShaders() { removeShader(pRenderer, pSpriteShader); }

	void addPipelines()
	{
		RasterizerStateDesc rasterizerStateDesc = {};
		rasterizerStateDesc.mCullMode = CULL_MODE_NONE;

		DepthStateDesc depthStateDesc = {};
		depthStateDesc.mDepthTest = false;
		depthStateDesc.mDepthWrite = false;

		BlendStateDesc blendStateDesc = {};
		blendStateDesc.mSrcAlphaFactors[0] = BC_SRC_ALPHA;
		blendStateDesc.mDstAlphaFactors[0] = BC_ONE_MINUS_SRC_ALPHA;
		blendStateDesc.mSrcFactors[0] = BC_SRC_ALPHA;
		blendStateDesc.mDstFactors[0] = BC_ONE_MINUS_SRC_ALPHA;
		blendStateDesc.mColorWriteMasks[0] = COLOR_MASK_ALL;
		blendStateDesc.mRenderTargetMask = BLEND_STATE_TARGET_0;
		blendStateDesc.mIndependentBlend = false;

		VertexLayout vertexLayout = {};
		vertexLayout.mBindingCount = 1;
		vertexLayout.mAttribCount = 1;
		vertexLayout.mAttribs[0].mSemantic = SEMANTIC_POSITION;
		vertexLayout.mAttribs[0].mFormat = TinyImageFormat_R32_SFLOAT;
		vertexLayout.mAttribs[0].mBinding = 0;
		vertexLayout.mAttribs[0].mLocation = 0;
		vertexLayout.mAttribs[0].mOffset = 0;

		// VertexLayout for sprite drawing.
		PipelineDesc desc = {};
		desc.mType = PIPELINE_TYPE_GRAPHICS;
		PIPELINE_LAYOUT_DESC(desc, SRT_LAYOUT_DESC(SrtData, Persistent), SRT_LAYOUT_DESC(SrtData, PerFrame), NULL, NULL);
		GraphicsPipelineDesc& pipelineSettings = desc.mGraphicsDesc;
		pipelineSettings.mPrimitiveTopo = PRIMITIVE_TOPO_TRI_LIST;
		pipelineSettings.mRenderTargetCount = 1;
		pipelineSettings.pDepthState = &depthStateDesc;
		pipelineSettings.pColorFormats = &pSwapChain->ppRenderTargets[0]->mFormat;
		pipelineSettings.mSampleCount = pSwapChain->ppRenderTargets[0]->mSampleCount;
		pipelineSettings.mSampleQuality = pSwapChain->ppRenderTargets[0]->mSampleQuality;
		pipelineSettings.mDepthStencilFormat = TinyImageFormat_UNDEFINED;
		pipelineSettings.pShaderProgram = pSpriteShader;
		pipelineSettings.pRasterizerState = &rasterizerStateDesc;
		pipelineSettings.pBlendState = &blendStateDesc;
		pipelineSettings.pVertexLayout = &vertexLayout;
		addPipeline(pRenderer, &desc, &pSpritePipeline);
	}

	void removePipelines() { removePipeline(pRenderer, pSpritePipeline); }

	void prepareDescriptorSets()
	{
		DescriptorData params[2] = {};
		params[0].mIndex = SRT_RES_IDX(SrtData, Persistent, uTexture0);
		params[0].ppTextures = &pSpriteTexture;
		params[1].mIndex = SRT_RES_IDX(SrtData, Persistent, uSampler0);
		params[1].ppSamplers = &pLinearClampSampler;
		updateDescriptorSet(pRenderer, 0, pDescriptorSetTexture, TF_ARRAY_COUNT(params), params);

		for (uint32_t i = 0; i < gDataBufferCount; ++i)
		{
			DescriptorData perFrame[1] = {};
			perFrame[0].mIndex = SRT_RES_IDX(SrtData, PerFrame, instanceBuffer);
			perFrame[0].ppBuffers = &pSpriteVertexBuffers[i];
			updateDescriptorSet(pRenderer, i, pDescriptorSetUniforms, 1, perFrame);
		}
	}
};

DEFINE_APPLICATION_MAIN(EntityComponentSystem)
