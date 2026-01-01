# Pre-Skinned Animations in Visibility Buffer

This document covers how Pre-Skinned Animations is implemented in TheForge and how Animation support affects the different Visibility Buffer Stages.

The sections in this file are:
  - Introduction
  - Vertex Buffers
    - Buffers for Skinned Attributes vs Non-Skinned Attributes
    - Joints and Weights Vertex Buffers
  - Pre-Skinning Stage
  - Triangle Filtering Stage
  - Visibility Buffer Shade Stage
  - Pre-Skin Stage and Async Compute 
    - Buffer Aliasing
    - Buffer Split

## Introduction

There are two main ways to implement skinning in VisibilityBuffer:
  1. Pre-Skin the vertexes before Triangle Filtering stage and then let the skinned vertexes enter the rest of the stages as normal static triangles. Pros: We only skin vertexes once; Cons: We need additional memory, we need to store skinned vertexes for each animated instance
  2. Apply skinning matrixes at each stage of the Visibility Buffer (triangle filtering, visibility buffer fill, visibility buffer shade). Pros: We don't need additional memory for each instance; Cons: We need to perform skinning multiple times, computationaly expensive.

This file only covers option 1, the one implemented in UnitTest 15a_VisibilityBuffer_OIT.
We chose to implement this version because it gives us several benefits:
  - only requires us to skin the vertexes once in the entire frame;
  - we use a compute shader before the triangle-filtering stage where we skin all instances of all meshes at the same time, one shader dispatch to skin everything;
  - we can run the pre-skin compute shader asynchronously, pre-skinning and triangle-filtering happen in the Compute Queue (parallel to the Graphics Queue)

## Vertex Buffers

Visibility Buffer uses one single huge Vertex Buffer containing all geometry in the scene.
To implement pre-skin animations we have to allocate extra space in this buffer for each animated instance we'll have in our scene, then during the Pre-Skin stage we store skinned vertexes for each animated instance in this buffer.

Here's an example on how this buffer could be divided:
```
The memory for the buffer (one attribute, e.g. Positions):
|--------------------------------- Vertex Buffer Memory ------------------------------------|

Here's how we divide the memory above, Pre-Skinned Data could be at the beggining or in the middle, it doesn't matter as long as we track offsets properly:
|--------------------- Static Geometry ---------------------|------ Pre-Skinned Data -------|
```

The **Static Geometry** chunk above contains all the world geometry in world space and also the skinned geometry in model space, think of them as the meshes we load from file.
The Pre-Skin shader will read the vertexes in Static Geometry, skin them and store the skinned vertex in model space in the **Pre-Skinned Data** chunk.

### Buffers for Skinned Attributes vs Non-Skinned Attributes

We only need Pre-Skinned data for the skinned attributes (positions, normals, tangents), for this reason the Vertex Buffers we use look like the following:

Vertex Buffer Diagram:
```
Positions: |--------------------- Static Geometry ---------------------|------ Pre-Skinned Data ------|
Normals:   |--------------------- Static Geometry ---------------------|------ Pre-Skinned Data ------|
Tangents:  |--------------------- Static Geometry ---------------------|------ Pre-Skinned Data ------|
UVs:       |--------------------- Static Geometry ---------------------|
...:       |--------------------- Static Geometry ---------------------|
```

The indexes we store in the FilteredIndexBuffer for skinned triangles are the indexes into the **Pre-Skinned Data** chunk.
Then, during Visibility Buffer Shade stage we know if a triangle belongs to an skinned mesh and we read skinned attributes from **Pre-Skinned Data** chunk and the rest of attributes (like UVs) from the **Static Geometry** chunks.
This way we save memory and we don't have to copy non-skinned attributes multiple times.

### Joints and Weights Vertex Buffers

The Joints and Weights Vertex Buffers are only used by the `pre_skin_vertexes.comp` shader, we can make them as small as we can as long as there's enough space to hold all the joints/weights for all the animated meshes we want to have loaded at once.
The index of a vertex in the Joints and Weights vertex buffers doesn't match with the index of the same vertex stored in the other buffers.
We pass correct offsets into these buffers to the `pre_skin_vertexes.comp` shader, this is explained below in the **Pre-Skinning Stage** section below.
Notice how the buffers below are much smaller in size than the Vertex Buffers above.

```
Joints:  |--------- Joint Data ---------|
Weights: |--------- Weight Data --------|
```

## Pre-Skinning Stage

The Pre-Skinning stage is a new stage in the VisibilityBuffer pipeline that is run before Triangle Filtering, adding this stage, the Visibility Buffer stages look like this:
  1. (compute) Pre-Skinning
  2. (compute) Triangle Filtering
  3. (graphics) Visibility Buffer Fill
  4. (graphics) Visibility Buffer Shade

The Pre-Skin stage runs a compute shader `pre_skin_vertexes.comp`.
This shader works in a similar way to the triangle-filtering shader, during triangle-filtering we filter batches of triangles comming in `struct FilterBatchData`, during pre-skinning we skin vertex batches comming in `struct PreSkinBatchData`. Each batch contains 256 vertexes of a given mesh instance.

This shader uses the following buffers, these buffers are set per frame:
  - `vertexPositionBuffer`, `vertexNormalBuffer`, `vertexTangentBuffer`: In/Out buffers, these are the Vertex Buffers that contain the original mesh vertexes (in the **Static Geometry** chunk, see above) that we read to apply skinning and we then write the ouput skinned vertexes on these same buffers at a reserved offset for the pre-skinned data (we output data in the **Pre-Skinned Data** chunk). See the **Vertex Buffer Diagram** above.
  - `vertexJointsBuffer`, `vertexWeightsBuffer`: In vertex data, smaller than Position/Normal/Tangent buffers
  - `jointMatrixes`: Contains the matrixes for all animated instances that will be rendered, the offset to the start of the matrixes of each instance comes in `PreSkinBatchData`.

Below is an example distribution of a `jointMatrixes` array for a given frame. 
Notice that different instances can have different number of matrixes, this is because the number of matrixes depends on the number of joints a mesh has.
The Pre-Skin pass skins all instances that we render, no matter if they come from the same mesh or not.
```
|---------------------------- jointMatrixes memory ----------------------------|
|--- Instance 1 ---|- Inst 2 -|--- Instance 3 ---|----- Instance 4 -----|------|
```

Each `pre_skin_vertexes.comp` dispatch takes as input an array of `PreSkinBatchData` as Root CBV, this is the data that tells us what vertexes to skin.
Below is the definition of `PreSkinBatchData`.

```
// Input to pre skin vertexes shader
STRUCT(PreSkinBatchData)
{
	/**********************************/
	// Per Batch
	/**********************************/
	DATA(uint, vertexCount, None);         // Number of vertexes to be skinned by this batch (usually equals SKIN_BATCH_SIZE)

	// Output vertexes are written in the range [outputVertexOffset, outputVertexOffset + vertexCount)
	DATA(uint, outputVertexOffset, None);

	// Offsets to the start of the mesh in the huge Vertex Buffers
	// Position offset is different to Joint offset because the Joints/Weights buffers are smaller
	DATA(uint, vertexPositionOffset, None);
	DATA(uint, vertexJointsOffset, None);

	/**********************************/
	// Per Instance
	/**********************************/
	DATA(uint, jointMatrixOffset, None); 	// Offset into the matrix buffer that contains all matrixes for all animated objects

	// Padding
	DATA(uint, pad0, None);
	DATA(uint, pad1, None);
	DATA(uint, pad2, None);
};
```

Pseudo code of how the `pre_skin_vertexes.comp` shader processes the data, code below uses the resources descrived above:
```
for batch in batches
	for v = 0 to batch.vertexCount
		readVertexIndex = batch.vertexPositionOffset + v
		readJointIndex = batch.vertexJointsOffset + v
		writeVertexIndex = batch.outputVertexOffset + v

		vertexJoints  = vertexJointsBuffer[readJointIndex]
		vertexWeights = vertexWeightsBuffer[readJointIndex]
		
		// Joint indexes for each mesh are relative to 0, we need to offset them to the correct index in `jointMatrixes`,
		// each animated Instance will have a different jointMatrixOffset
		vertexJoints += batch.jointMatrixOffset

		// Repeat code below for Normals and Tangents
		bindPoseVertexModelSpace = vertexPositionBuffer[readVertexIndex]
		skinPoseVertexModelSpace = Skin(bindPoseVertexModelSpace, vertexJoints, vertexWeights, jointMatrixes)
		vertexPositionBuffer[writeVertexIndex] = skinPoseVertexModelSpace
```

After this stage the animated meshes enter the rest of the Visibility Buffer stages in the same way Static Mesh Instances do, we'll transform the vertexes by a model matrix in each of the stages.
There's an option to transform the vertexes to world space during the Pre-Skin stage and avoid the model matrix multiplication in the rest of the stages, we've decided not to do this for two main reasons:
  1. We reuse the Static Mesh Instance logic, the code is more consistent. We still need to treat Animated meshes as instances since we need to pass an offset in the `InstanceData` for animation, see section **Visibility Buffer Shade Stage**
  2. By storing the skinned mesh in model space we can then instantiate the mesh multiple times to simulate huge crowds, by reusing the same skinned mesh in different places. One mesh instance is skinned but then multiple mesh instances are with different model matrixes

## Triangle Filtering Stage

Triangle Filtering shader reads vertexes in the **Pre-Skinned Data** chunk of the Vertex Buffer, transforms them to world and applies the filtering.
If the triangle is not filtered then it writes the triangle indexes into the **Filtered Index Buffer**.

The written indexes are relative to the start of the mesh, they are in the range `[0, meshVertexCount)`, then in the indirect draw arguments we write the vertexOffset which offsets to the **Pre-Skinned Data** chunk of the vertex buffer.
It's important to keep the indexes relative to the start of the mesh because we need to be able to read attributes at two different offsets of the Vertex Buffers, skinned attributes from the **Pre-Skinned Data** chunk and the rest from the **Static Geometry** chunk.
This is covered in more detail in the next section.

## Visibility Buffer Shade Stage

We have two types of attributes to read:
  - Skinned: Positions, Normals, Tangents
  - Constant (static): UVs, etc (the rest)

During shading we read the indexes in the **Filtered Index Buffer**, which are relative to the mesh, and apply the `vertexOffset` in the `Indirect Draw Arguments`.
If the triangle is not skinned we are done, we have the final offset for all attributes, which is `filteredIndex + indirectDrawArgs.vertexOffset`.
On the other hand, if the triangle is skinned, the resulting index is only valid for the Pre-Skinned attributes and we have to compute another index for the constant attributes.

Pseudo code for skinned/constant index computation and usage:
```
skinnedAttributeVertexOffset = indirectDrawArgs.vertexOffset
constAttributeVertexOffset = indirectDrawArgs.vertexOffset

if isTriangleSkinned
	// skinnedAttributeVertexOffset already contains the offset to the Pre-Skinned Data chunk, nothing to change there

	// Use indexes from the Static Geometry chunk
	// meshConstants is a struct that contains information about a given mesh, vertexOffset is the offset to the start of this mesh in the Static Geometry chunk
	constAttributeVertexOffset = meshConstants.vertexOffset
else
	// This means triangle is not skinned and we read skinned/const attributes from the same offset in the Static Geometry chunk of the Vertex Buffer

// Use skinnedAttributeVertexOffset to read Skinned attributes: Position, Normal, Tangent
// Use constAttributeVertexOffset to read Constant attributes: UVs, etc (the rest)
```

## Pre-Skin Stage and Async Compute 

Note: the concepts explained in this section regarding Buffer Aliasing and multiple **Pre-Skinned Data** chunks only applies if the Application uses Async Compute, if not all these are not required.

Running just **Triangle Filtering Stage** asynchronously in the Compute queue is simple since we just take a read-only Positions Vertex Buffer as input and we generate the filtered index buffer and indirect draw calls.
However when we take Animations into account things are more complicated, the **Pre-Skin Stage** needs to run before the **Triangle Filtering Stage**.
The Pre-Skin vertexes compute shader requires the vertexBuffers to be in read-write state, while in the work we do while rendering the scene in the Graphics queue we need the buffers to be in vertex buffer state.
This means we cannot use the same buffers in the Compute Queue and the Graphics Queue.
The way we decided to solve this problem is using Buffer Aliasing.

### Buffer Aliasing

The simple explanation is that we allocate one chunk of memory for the Skinned Vertex Buffers and bind several buffers to it.
This way we can use the same memory on the Graphics Queue and the Compute Queue.
Here's a diagram of how it looks like:

```
Allocated Vertex Buffer Memory:                |----------------------------------------------|
Buffer used on the Graphics Queue (read only): ^..............................................^
Buffer used on the Compute Queue (read write): ^..............................................^

Note that only the |----| represents memory, the ^....^ just mark that the buffer uses that memory
```

### Buffer Split

When using Async Compute we need to have more than one **Pre-Skinned Data** chunk, one is being written to in the Compute Queue on the **Pre-Skin Stage** while the other is read in the Graphics Queue for rendering the previous frame.

In the following diagram you can see how we have multiple **Pre-Skinned Data** chunks:
```
|--------------------- Static Geometry ---------------------|----- Pre-Skinned Data 0 -----|----- Pre-Skinned Data 1 -----|
```


