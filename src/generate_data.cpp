#include <block_utility.h>
#include <texture_utility.h>

// NOTE: right now, I'm manually loading the texture info of each block face
// In the future, all texture types, block types, block info for each type shall be 
// contained in a json file, the data from this file will be used to construct the appropriate enums
// block info maps.

void generate_data(){
    blockTexMap = new BlockInfo[NUM_BLOCK_TYPES];

    blockTexMap[BlockMap::grass].texId[blockDirectionIndex::top] = TexMap::grass_cover;
    blockTexMap[BlockMap::grass].texId[blockDirectionIndex::bottom] = TexMap::side_dirt;
    blockTexMap[BlockMap::grass].texId[blockDirectionIndex::left] = TexMap::side_dirt;
    blockTexMap[BlockMap::grass].texId[blockDirectionIndex::right] = TexMap::side_dirt;
    blockTexMap[BlockMap::grass].texId[blockDirectionIndex::front] = TexMap::side_dirt;
    blockTexMap[BlockMap::grass].texId[blockDirectionIndex::back] = TexMap::side_dirt;

    // --- AIR ---
    // Zero out the air block data just to be safe.
    blockTexMap[BlockMap::air] = {};
}