#include <resource.h>

#include <iostream>

std::unordered_map<ResourceManager::ResourceID, Shader> ResourceManager::shaders;
std::unordered_map<ResourceManager::ResourceID, Texture> ResourceManager::textures;

Shader* ResourceManager::LoadShader(const std::string& vPath, const std::string& fPath, 
ResourceManager::ResourceID id){
    auto [it, inserted] = shaders.try_emplace(
        id,            
        vPath, fPath   
    );

    return &it->second;
}

Shader* ResourceManager::GetShader(ResourceManager::ResourceID id){
    auto it = shaders.find(id);
    if (it == shaders.end()){
        std::cerr << "Shader " << id << "not found, could not retrieve" << std::endl;
        return nullptr;
    }
    return &it->second;
}

void ResourceManager::DeleteShader(ResourceManager::ResourceID id){
    auto it = shaders.find(id);
    if (it == shaders.end()){
        std::cerr << "Shader " << id << "not found, could not delete" << std::endl;
        return;
    }

    it->second.cleanup();
    shaders.erase(it);
}


Texture* ResourceManager::LoadTexture(const std::string& path, 
ResourceManager::ResourceID id){
    auto [it, inserted] = textures.try_emplace(
        id,            
        path   
    );

    return &it->second;
}

Texture* ResourceManager::GetTexture(ResourceManager::ResourceID id){
    auto it = textures.find(id);
    if (it == textures.end()){
        std::cerr << "Texture " << id << "not found, could not retrieve" << std::endl;
        return nullptr;
    }
    return &it->second;
}

void ResourceManager::DeleteTexture(ResourceManager::ResourceID id){
    auto it = textures.find(id);
    if (it == textures.end()){
        std::cerr << "Texture " << id << "not found, could not delete" << std::endl;
        return;
    }

    it->second.cleanup();
    textures.erase(it);
}

void ResourceManager::deleteAllShaders() {
    for (auto& [_, shader] : shaders) {
        shader.cleanup();
    }
    shaders.clear();
}

void ResourceManager::deleteAllTextures() {
    for (auto& [_, texture] : textures) {
        texture.cleanup();
    }
    textures.clear();
}

void ResourceManager::deleteAll() {
    for (auto& [_, shader] : shaders) shader.cleanup();
    for (auto& [_, texture] : textures) texture.cleanup();
    shaders.clear();
    textures.clear();
}
