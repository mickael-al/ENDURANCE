"""Rasterize the production G-buffer/shadow stages into a small test FBO."""
import ctypes as C
import math
import struct

from gl_headless import shader_source


def check_render(gl, directory):
    signatures = {
        "glGenFramebuffers": (None,C.c_int,C.POINTER(C.c_uint)),
        "glBindFramebuffer": (None,C.c_uint,C.c_uint),
        "glDeleteFramebuffers": (None,C.c_int,C.POINTER(C.c_uint)),
        "glGenTextures": (None,C.c_int,C.POINTER(C.c_uint)),
        "glBindTexture": (None,C.c_uint,C.c_uint),
        "glDeleteTextures": (None,C.c_int,C.POINTER(C.c_uint)),
        "glTexImage2D": (None,C.c_uint,C.c_int,C.c_int,C.c_int,C.c_int,C.c_int,C.c_uint,C.c_uint,C.c_void_p),
        "glFramebufferTexture2D": (None,C.c_uint,C.c_uint,C.c_uint,C.c_uint,C.c_int),
        "glDrawBuffers": (None,C.c_int,C.POINTER(C.c_uint)),
        "glCheckFramebufferStatus": (C.c_uint,C.c_uint),
        "glViewport": (None,C.c_int,C.c_int,C.c_int,C.c_int),
        "glClearBufferfv": (None,C.c_uint,C.c_int,C.POINTER(C.c_float)),
        "glGenVertexArrays": (None,C.c_int,C.POINTER(C.c_uint)),
        "glDeleteVertexArrays": (None,C.c_int,C.POINTER(C.c_uint)),
        "glBindVertexArray": (None,C.c_uint),
        "glEnableVertexAttribArray": (None,C.c_uint),
        "glVertexAttribIPointer": (None,C.c_uint,C.c_int,C.c_uint,C.c_int,C.c_void_p),
        "glDrawArraysIndirect": (None,C.c_uint,C.c_void_p),
        "glDrawArraysInstancedBaseInstance": (None,C.c_uint,C.c_int,C.c_int,C.c_int,C.c_uint),
        "glBeginTransformFeedback": (None,C.c_uint),
        "glEndTransformFeedback": (None,),
        "glReadBuffer": (None,C.c_uint),
        "glReadPixels": (None,C.c_int,C.c_int,C.c_int,C.c_int,C.c_uint,C.c_uint,C.c_void_p),
        "glEnable": (None,C.c_uint),
        "glDisable": (None,C.c_uint),
    }
    for name, signature in signatures.items():
        gl._fn(name,*signature)
    programs={}
    for stem in ("render","shadow"):
        programs[stem]=gl.compile([(0x8B31,shader_source(directory/(stem+".vs.glsl"))),
                                   (0x8B30,shader_source(directory/(stem+".fs.glsl")))],stem)
    textures=(C.c_uint*5)()
    framebuffer,vao=C.c_uint(),C.c_uint()
    gl.glGenTextures(5,textures)
    gl.glGenFramebuffers(1,C.byref(framebuffer))
    gl.glGenVertexArrays(1,C.byref(vao))
    try:
        gl.glBindFramebuffer(0x8D40,framebuffer.value)
        for index in range(5):
            gl.glBindTexture(0x0DE1,textures[index])
            depth=index==4
            gl.glTexImage2D(0x0DE1,0,0x8CAC if depth else 0x8814,8,8,0,0x1902 if depth else 0x1908,0x1406,None)
            gl.glFramebufferTexture2D(0x8D40,0x8D00 if depth else 0x8CE0+index,0x0DE1,textures[index],0)
        attachments=(C.c_uint*4)(*(0x8CE0+i for i in range(4)))
        gl.glDrawBuffers(4,attachments)
        assert gl.glCheckFramebufferStatus(0x8D40)==0x8CD5,"Incomplete test G-buffer"
        gl.glViewport(0,0,8,8)
        gl.glEnable(0x0B71) # GL_DEPTH_TEST
        identity=[1.,0,0,0,0,1.,0,0,0,0,1.,0,0,0,0,1.]
        view=identity.copy();view[14]=-2.25
        inverse=identity.copy();inverse[14]=2.25
        projection=identity.copy();projection[0]=projection[5]=.5
        camera=gl.buffer(struct.pack("<52f",*( [0,0,2.25,0]+view+inverse+projection)),0)
        # Slot 0 is intentionally invalid: gl_BaseInstance must select slot 1.
        gl.buffer(struct.pack("<4f4I4f4I",100,100,100,1,0,100,100,100,-2,-2,0,4,1,0xffffffff,0xffffffff,0),8)
        encoded=[]
        for x,y in ((0,0),(65534,0),(32767,65534)):
            encoded.extend((x|(y<<16),32767|(128<<16)|(192<<24),0)) # normal +Z
        vertex=gl.buffer(struct.pack("<9I",*encoded))
        gl.glBindVertexArray(vao.value)
        gl.glBindBuffer(0x8892,vertex)
        for index in range(3):
            gl.glVertexAttribIPointer(index,1,0x1405,12,C.c_void_p(index*4))
            gl.glEnableVertexAttribArray(index)
        indirect=gl.buffer(struct.pack("<4I",3,1,0,1))
        gl.glBindBuffer(0x8F3F,indirect)
        render=programs["render"]
        gl.uniform(render,"microDetailStrength","1f",0)
        gl.uniform(render,"microDetailScale","1f",1)
        gl.uniform(render,"worldSeed","1ui",1337)
        gl.uniform(render,"debugLod","1i",0)
        for index in range(4):
            gl.glClearBufferfv(0x1800,index,(C.c_float*4)(-77,-77,-77,-77))
        gl.glClearBufferfv(0x1801,0,(C.c_float*1)(1))
        gl.glUseProgram(render)
        gl.glDrawArraysIndirect(0x0004,None)
        def sample(attachment):
            gl.glReadBuffer(0x8CE0+attachment)
            value=(C.c_float*4)()
            gl.glReadPixels(4,3,1,1,0x1908,0x1406,value)
            return tuple(value)
        position,normal,color,material=(sample(i) for i in range(4))
        assert all(abs(a-b)<1e-5 for a,b in zip(position,(.25,-.25,-.25,0))), ("G-buffer view position/projection",position)
        assert all(abs(a-b)<1e-5 for a,b in zip(normal[:3],(0,0,1))), ("G-buffer normal",normal)
        assert all(math.isfinite(v) and 0<=v<=1 for v in color) and color[3]==1,("G-buffer color",color)
        assert all(abs(a-b)<1e-5 for a,b in zip(material[:3],(0,.88,1))), ("G-buffer material",material)
        # Reuse the geometry through the exact cascade shadow path and check the
        # resulting depth. The invalid first cascade also catches offset errors.
        projection_view=projection.copy();projection_view[14]=-2.25
        invalid=identity.copy();invalid[14]=100
        gl.buffer(struct.pack("<40f",*(invalid+[0,0,0,0]+projection_view+[0,0,0,0])),0)
        gl.glDrawBuffers(0,None)
        gl.glClearBufferfv(0x1801,0,(C.c_float*1)(1))
        gl.uniform(programs["shadow"],"offsetShadow","1i",1)
        gl.glDrawArraysIndirect(0x0004,None)
        depth=C.c_float()
        gl.glReadPixels(4,3,1,1,0x1902,0x1406,C.byref(depth))
        assert abs(depth.value-.375)<1e-5,("Shadow cascade projection",depth.value)
        gl.check("G-buffer/shadow rasterization")
        print("PASS rendering: indirect descriptor selection, camera projection, four G-buffer attachments and cascade shadow depth")
        gl.bind(camera,0)
        check_canonical_decode(gl,directory)
    finally:
        gl.glDisable(0x0B71)
        gl.glBindVertexArray(0)
        gl.glBindFramebuffer(0x8D40,0)
        gl.glDeleteVertexArrays(1,C.byref(vao))
        gl.glDeleteFramebuffers(1,C.byref(framebuffer))
        gl.glDeleteTextures(5,textures)


def check_canonical_decode(gl,directory):
    """Capture the real vertex shader's worldPosition as raw float32 bytes."""
    program=gl.compile([(0x8B31,shader_source(directory/"render.vs.glsl")),
                        (0x8B30,shader_source(directory/"render.fs.glsl"))],
                       "canonical decode transform feedback",feedback=("worldPosition",))
    descriptors=bytearray()
    vertices=[]
    pair_count=0
    # Integer base coordinates are in units of one base chunk (64m here).
    # Both signs, quarter/half interpolation and large legal LODs are covered.
    for lod in (1,5,12,20):
        stride=1<<lod
        for sign in (-1,1):
            origin=[sign*stride*8,sign*stride*3,-sign*stride*5]
            for qy,qz in ((40000,12345),(32767,32767),(49151,16384),(65534,0)):
                coarse=(65534,qy,qz)
                parity_y=int(qy>=32767)
                parity_z=int(qz>=32767)
                fine=(0,2*qy-parity_y*65534,2*qz-parity_z*65534)
                fine_origin=[origin[0]+stride,origin[1]+parity_y*(stride//2),origin[2]+parity_z*(stride//2)]
                for point,chunk_origin,chunk_lod in ((coarse,origin,lod),(fine,fine_origin,lod-1)):
                    descriptors.extend(struct.pack("<4f4I",*(float(v*64) for v in chunk_origin),
                                                   float(64*(1<<chunk_lod)),chunk_lod,
                                                   *(v&0xffffffff for v in chunk_origin)))
                    vertices.extend((point[0]|(point[1]<<16),point[2],0))
                pair_count+=1
    gl.buffer(bytes(descriptors),8)
    vertex=gl.buffer(struct.pack(f"<{len(vertices)}I",*vertices))
    gl.glBindBuffer(0x8892,vertex)
    for index in range(3):
        gl.glVertexAttribIPointer(index,1,0x1405,12,C.c_void_p(index*4))
    output=gl.buffer(pair_count*2*12)
    gl.glBindBufferBase(0x8C8E,0,output)
    gl.glEnable(0x8C89) # GL_RASTERIZER_DISCARD
    try:
        gl.glUseProgram(program)
        gl.glBeginTransformFeedback(0) # GL_POINTS
        for index in range(pair_count*2):
            gl.glDrawArraysInstancedBaseInstance(0,index,1,1,index)
        gl.glEndTransformFeedback()
        raw=gl.read(output,pair_count*2*12)
        for index in range(pair_count):
            coarse,fine=raw[index*24:index*24+12],raw[index*24+12:index*24+24]
            assert coarse==fine,("LOD vertex world coordinates differ",index,struct.unpack("<3f",coarse),struct.unpack("<3f",fine))
            assert all(math.isfinite(v) for v in struct.unpack("<3f",coarse))
        gl.check("canonical world decode")
        print(f"PASS vertex decode: {pair_count} coarse/fine seam positions are bit-identical at positive/negative coordinates, LOD1–20")
    finally:
        gl.glDisable(0x8C89)
        gl.glBindBufferBase(0x8C8E,0,0)
