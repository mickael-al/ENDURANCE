// Included by one biome's relief.C.glsl after its guarded 2D function.
layout(local_size_x=8,local_size_y=8,local_size_z=1) in;
layout(std430,binding=1) buffer FieldColumns { FieldColumn columns[]; };
void main()
{
    uint listIndex=gl_WorkGroupID.z;
    if(listIndex>=fieldCommands[FIELD_BIOME_ID].w) return;
    uint job=fieldWorkItems[FIELD_BIOME_ID*FIELD_MAX_JOBS+listIndex];
    uvec2 p=gl_GlobalInvocationID.xy;
    uint side=uCells+3u;
    if(any(greaterThanEqual(p,uvec2(side)))) return;
    fieldSelectJob(job);
    uint address=job*uColumnStride+p.x+p.y*side;
    FieldColumn column=columns[address];
    float weight=fieldSurfaceWeight(column,FIELD_BIOME_ID);
    if(weight<=0.0) return;
    vec2 world=fieldWorldPosition(ivec3(p.x,1,p.y)).xz;
    vec4 relief=fieldClampRelief(FIELD_RELIEF_FUNCTION(world));
    // One writer per column in a pass. Sequential passes need storage barriers,
    // not float atomics or one field allocation per biome.
    precise vec4 accumulated=vec4(column.terrain.x,column.terrain.w,column.blend.zw)+relief*weight;
    column.terrain.x=accumulated.x;
    column.terrain.w=accumulated.y;
    column.blend.zw=accumulated.zw;
    columns[address]=column;
}
