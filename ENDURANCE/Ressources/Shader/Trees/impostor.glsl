void treeViews(vec3 facing,out ivec3 ids,out vec3 weights)
{
    float yaw=mod(atan(facing.x,facing.z)/0.7853981634+8.0,8.0);
    int i=int(floor(yaw)),j=(i+1)%8; float f=fract(yaw);
    float pitch=degrees(atan(facing.y,length(facing.xz)));
    if(pitch<=15.0) { ids=ivec3(i,j,i); weights=vec3(1.0-f,f,0); }
    else if(pitch<60.0)
    {
        float v=(pitch-15.0)/45.0;
        if(f+v<=1.0) { ids=ivec3(i,j,8+i); weights=vec3(1.0-f-v,f,v); }
        else { ids=ivec3(8+j,8+i,j); weights=vec3(f+v-1.0,1.0-f,1.0-v); }
    }
    else
    {
        float v=clamp((pitch-60.0)/30.0,0.0,1.0);
        ids=ivec3(8+i,8+j,16); weights=vec3((1.0-v)*(1.0-f),(1.0-v)*f,v);
    }
}
mat3 treeCaptureBasis(int index)
{
    if(index==16) return mat3(vec3(1,0,0),vec3(0,0,-1),vec3(0,1,0));
    float yaw=float(index%8)*0.7853981634, pitch=index<8?0.2617993878:1.0471975512;
    vec3 d=vec3(cos(pitch)*sin(yaw),sin(pitch),cos(pitch)*cos(yaw));
    vec3 r=vec3(cos(yaw),0,-sin(yaw));
    return mat3(r,cross(d,r),d);
}
