#pragma once
// NovaEngine 总伞头：包含全部子模块公共头

#include <NovaEngine/Core/Window.h>
#include <NovaEngine/Core/Input.h>
#include <NovaEngine/Core/InputMap.h>
#include <NovaEngine/Core/Time.h>
#include <NovaEngine/Core/Logger.h>
#include <NovaEngine/Core/UUID.h>
#include <NovaEngine/Core/Stats.h>
#include <NovaEngine/Core/Events.h>

#include <NovaEngine/Render/Renderer.h>
#include <NovaEngine/Render/Shader.h>
#include <NovaEngine/Render/Mesh.h>
#include <NovaEngine/Render/MeshData.h>
#include <NovaEngine/Render/Texture.h>
#include <NovaEngine/Render/Material.h>
#include <NovaEngine/Render/Framebuffer.h>
#include <NovaEngine/Render/Light.h>
#include <NovaEngine/Render/Primitives.h>
#include <NovaEngine/Render/ShadowMap.h>
#include <NovaEngine/Render/SkyboxRenderer.h>
#include <NovaEngine/Render/Cubemap.h>
#include <NovaEngine/Render/PostProcess.h>

#include <NovaEngine/Scene/GameObject.h>
#include <NovaEngine/Scene/Component.h>
#include <NovaEngine/Scene/ComponentRegistry.h>
#include <NovaEngine/Scene/Transform.h>
#include <NovaEngine/Scene/Scene.h>
#include <NovaEngine/Scene/SceneManager.h>
#include <NovaEngine/Scene/SceneRenderer.h>
#include <NovaEngine/Scene/MeshRenderer.h>
#include <NovaEngine/Scene/Camera.h>
#include <NovaEngine/Scene/DirectionalLight.h>
#include <NovaEngine/Scene/Rigidbody.h>
#include <NovaEngine/Scene/Collider.h>
#include <NovaEngine/Scene/AudioListener.h>
#include <NovaEngine/Scene/AudioSource.h>
#include <NovaEngine/Scene/Canvas.h>
#include <NovaEngine/Scene/Image.h>
#include <NovaEngine/Scene/Text.h>
#include <NovaEngine/Scene/Button.h>
#include <NovaEngine/Scene/PrefabInstance.h>
#include <NovaEngine/Scene/LODGroup.h>

#include <NovaEngine/Assets/AssetDatabase.h>
#include <NovaEngine/Assets/AssetStream.h>
#include <NovaEngine/Assets/BinaryCache.h>
#include <NovaEngine/Assets/MaterialAsset.h>
#include <NovaEngine/Assets/ModelLoader.h>
#include <NovaEngine/Assets/PakArchive.h>
#include <NovaEngine/Assets/TextureLoader.h>
#include <NovaEngine/Assets/TextureMeta.h>

#include <NovaEngine/Scripting/Behaviour.h>
#include <NovaEngine/Scripting/Coroutine.h>
#include <NovaEngine/Scripting/ScriptRegistry.h>

#include <NovaEngine/Physics/PhysicsTypes.h>
#include <NovaEngine/Physics/PhysicsWorld.h>

#include <NovaEngine/Audio/AudioEngine.h>
#include <NovaEngine/Audio/AudioClip.h>

#include <NovaEngine/UI/FontAtlas.h>
#include <NovaEngine/UI/UIRenderer.h>
#include <NovaEngine/UI/Slider.h>
#include <NovaEngine/UI/ScrollView.h>

#include <NovaEngine/SaveGame/SaveGame.h>

// ===== 波 2 渲染模块（V0.5-4/5/6，2026-11-19 合波接线）=====
#include <NovaEngine/Render/IBL.h>
#include <NovaEngine/Render/SSAO.h>
#include <NovaEngine/Particles/ParticleSystem.h>
#include <NovaEngine/Particles/ParticleRenderer.h>

// ===== 波 2 2b 模块（V0.5-7/8/9/10，2026-11-19 2b 合波接线）=====
#include <NovaEngine/Scene/SpriteRenderer.h>
#include <NovaEngine/Scene/BoxCollider2D.h>
#include <NovaEngine/Physics/Physics2D.h>
#include <NovaEngine/Terrain/Terrain.h>
#include <NovaEngine/Terrain/TerrainRenderer.h>
#include <NovaEngine/Animation/SkeletalMeshRenderer.h>
#include <NovaEngine/Animation/Animator.h>
#include <NovaEngine/Scripting/ScriptRuntime.h>

#include <NovaEngine/Serialization/PrefabSerializer.h>
#include <NovaEngine/Serialization/SceneSerializer.h>

// ===== V1.0 波 1 骨架日（§26.0 冻结项 5，2026-11-20）=====
#include <NovaEngine/Profiling/Profiler.h>
#include <NovaEngine/Assets/AssetReferenceScanner.h>
