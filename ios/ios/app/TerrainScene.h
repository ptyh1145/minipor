// Loads a packed asset bundle from Documents and turns it into GPU-ready terrain.
#import <Foundation/Foundation.h>

#include <cstdint>
#include <vector>

#include "miniworld/terrain.h"

// One vertex as uploaded to Metal: position, uv, texture-array slice, face shade.
struct MWVertex {
    float pos[3];
    float uv[2];
    float slice;
    float shade;
};

@interface MWTerrain : NSObject
@property(nonatomic) NSUInteger vertexCount;
@property(nonatomic) NSUInteger indexCount;
@property(nonatomic) NSUInteger sliceCount;
@property(nonatomic) NSUInteger sliceSize;
@property(nonatomic) NSString *label;
// texture array: sliceCount * sliceSize * sliceSize * 4 bytes, RGBA8
@property(nonatomic, strong) NSData *textureArray;
- (const MWVertex *)vertices;
- (const std::uint32_t *)indices;
@end

// Returns nil when Documents/assets.bin is missing or unreadable.
MWTerrain *MWLoadTerrainFromDocuments(void);
