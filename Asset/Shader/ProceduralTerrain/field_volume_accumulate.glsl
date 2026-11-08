// Included by one biome's volume.C.glsl after its 3D delta function.
layout(local_size_x=8,local_size_y=4,local_size_z=4) in;
layout(std430,binding=0) buffer FieldDensity { float density[]; };
layout(std430,binding=1) readonly buffer FieldColumns { FieldColumn columns[]; };
// Reuse each 48-byte column and its local weight across four Y samples.
// 32 XZ columns * (48 + 4 bytes) = 1664 bytes of shared workgroup storage.
shared FieldColumn localColumns[32];
shared float localWeights[32];
void main()
{
    const uint pass=FIELD_BIOME_COUNT+FIELD_BIOME_ID;
    uint listIndex;
    uvec3 p;
    fieldVolumeCoordinates(listIndex,p);
    if(listIndex>=fieldCommands[pass].w) return;
    uint job=fieldWorkItems[pass*FIELD_MAX_JOBS+listIndex];
    uint side=uCells+3u;
    fieldSelectJob(job);
    uint localColumn=gl_LocalInvocationID.x+8u*gl_LocalInvocationID.z;
    if(gl_LocalInvocationID.y==0u && p.x<side && p.z<side)
    {
        FieldColumn column=columns[job*uColumnStride+p.x+p.z*side];
        localColumns[localColumn]=column;
        localWeights[localColumn]=column.terrain.w>0.0002 ? fieldVolumeWeight(column,FIELD_BIOME_ID) : 0.0;
    }
    // Partial groups must reach this barrier before discarding their padding.
    barrier();
    if(any(greaterThanEqual(p,uvec3(side)))) return;
    float weight=localWeights[localColumn];
    if(weight<=0.0) return;
    FieldColumn column=localColumns[localColumn];
    vec3 world=fieldWorldPosition(ivec3(p));
    vec2 envelope=fieldVolumeEnvelope(column);
    if(world.y<envelope.x || world.y>envelope.y) return;
    uint address=job*uDensityStride+p.x+p.y*side+p.z*side*side;
    // The base was written once. Editable biome functions return only a delta.
    // precise also fixes contraction across independently compiled variants.
    precise float value=density[address]+weight*fieldVolume(world,column);
    density[address]=value;
}
