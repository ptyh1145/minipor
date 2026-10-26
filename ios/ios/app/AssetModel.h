// Builds the scene the render backend draws, straight out of the game's data pack:
//   * block table      csvdef/blockdef.csv   (decrypted in memory)
//   * world            worlds/*/m0/*.r       (48^3 voxels -> visible faces)
//   * model fallback   entities/*.omod
//   * texture          a block PNG, decoded to RGBA for Metal
//   * script probe     one .lua, decrypted and logged (no VM yet)
#pragma once

#import <Foundation/Foundation.h>

#include <cstdint>

#include "miniworld/mesh.h"

@interface MWModelData : NSObject
@property(nonatomic, copy) NSString *label;
@property(nonatomic) BOOL isWorld;
@property(nonatomic) NSUInteger vertexCount;
@property(nonatomic) NSUInteger indexCount;
@property(nonatomic, strong) NSData *textureRGBA;
@property(nonatomic) NSUInteger texWidth;
@property(nonatomic) NSUInteger texHeight;
- (const mw::MeshBuffer *)buffer;
@end

// Returns nil when there is no data pack at all (the app then draws the procedural cube).
MWModelData *MWLoadScene(void);
