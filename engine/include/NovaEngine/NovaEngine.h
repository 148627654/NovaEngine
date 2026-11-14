#pragma once
// NovaEngine 总伞头：包含全部子模块公共头

#include <NovaEngine/Core/Window.h>
#include <NovaEngine/Core/Input.h>
#include <NovaEngine/Core/Time.h>
#include <NovaEngine/Core/Logger.h>
#include <NovaEngine/Core/UUID.h>

#include <NovaEngine/Render/Renderer.h>
#include <NovaEngine/Render/Shader.h>
#include <NovaEngine/Render/Mesh.h>
#include <NovaEngine/Render/MeshData.h>
#include <NovaEngine/Render/Texture.h>
#include <NovaEngine/Render/Material.h>
#include <NovaEngine/Render/Framebuffer.h>
#include <NovaEngine/Render/Light.h>
#include <NovaEngine/Render/Primitives.h>

#include <NovaEngine/Scene/GameObject.h>
#include <NovaEngine/Scene/Component.h>
#include <NovaEngine/Scene/Transform.h>
#include <NovaEngine/Scene/Scene.h>
#include <NovaEngine/Scene/MeshRenderer.h>
#include <NovaEngine/Scene/Camera.h>
#include <NovaEngine/Scene/DirectionalLight.h>

#include <NovaEngine/Assets/AssetDatabase.h>
#include <NovaEngine/Assets/ModelLoader.h>
#include <NovaEngine/Assets/TextureLoader.h>

#include <NovaEngine/Scripting/Behaviour.h>
#include <NovaEngine/Scripting/ScriptRegistry.h>

#include <NovaEngine/Serialization/SceneSerializer.h>
