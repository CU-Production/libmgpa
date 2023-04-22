#include "mgba/core/core.h"
#include "mgba/core/thread.h"
#include "mgba/gba/core.h"
#include "mgba/internal/gba/gba.h"
#include "mgba/internal/gba/input.h" // For GBA key macro
#include "mgba/feature/commandline.h"

#define SOKOL_IMPL
#define SOKOL_NO_ENTRY
#define SOKOL_GLCORE33
#include "sokol_app.h"
#include "sokol_gfx.h"
#include "sokol_glue.h"
#include "sokol_log.h"

#include <iostream>
#include <mutex>

#define SOKOL_BINDING_KEY 0x22334455

unsigned width, height;
sg_pass_action pass_action{};
sg_buffer vbuf{};
sg_buffer ibuf{};
sg_pipeline pip{};
sg_bindings sgbind{};

mCoreThread thread{};
mCore* core;
mCoreSync* sync; // audio sync
mInputMap inputMap{};
color_t* outputBuffer;

void init() {
    sg_desc desc = {};
    desc.context = sapp_sgcontext();
    desc.logger.func = slog_func;
    sg_setup(&desc);

    const float vertices[] = {
            // positions     uv
            -1.0, -1.0, 0.0, 0.0, 1.0,
            1.0,  -1.0, 0.0, 1.0, 1.0,
            1.0,  1.0,  0.0, 1.0, 0.0,
            -1.0, 1.0,  0.0, 0.0, 0.0,
    };
    sg_buffer_desc vb_desc = {};
    vb_desc.data = SG_RANGE(vertices);
    vbuf = sg_make_buffer(&vb_desc);

    const int indices[] = { 0, 1, 2, 0, 2, 3, };
    sg_buffer_desc ib_desc = {};
    ib_desc.type = SG_BUFFERTYPE_INDEXBUFFER;
    ib_desc.data = SG_RANGE(indices);
    ibuf = sg_make_buffer(&ib_desc);

    sg_shader_desc shd_desc = {};
    shd_desc.attrs[0].name = "position";
    shd_desc.attrs[1].name = "texcoord0";
    shd_desc.vs.source = R"(
#version 330
layout(location=0) in vec3 position;
layout(location=1) in vec2 texcoord0;
out vec4 color;
out vec2 uv;
void main() {
  gl_Position = vec4(position, 1.0f);
  uv = texcoord0;
  color = vec4(uv, 0.0f, 1.0f);
}
)";
    shd_desc.fs.images[0].name = "tex";
    shd_desc.fs.images[0].image_type = SG_IMAGETYPE_2D;
    shd_desc.fs.images[0].sampler_type = SG_SAMPLERTYPE_FLOAT;
    shd_desc.fs.source = R"(
#version 330
uniform sampler2D tex;
in vec4 color;
in vec2 uv;
out vec4 frag_color;
void main() {
  frag_color = texture(tex, uv);
  //frag_color = pow(frag_color, vec4(1.0f/2.2f));
}
)";

    sg_image_desc img_desc = {};
    img_desc.width = width;
    img_desc.height = height;
    img_desc.label = "nes-texture";
    img_desc.pixel_format = SG_PIXELFORMAT_RGBA8;
    img_desc.usage = SG_USAGE_STREAM;

    sg_shader shd = sg_make_shader(&shd_desc);

    sg_pipeline_desc pip_desc = {};
    pip_desc.shader = shd;
    pip_desc.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT3;
    pip_desc.layout.attrs[1].format = SG_VERTEXFORMAT_FLOAT2;
    pip_desc.index_type = SG_INDEXTYPE_UINT32;
    pip = sg_make_pipeline(&pip_desc);

    sgbind.vertex_buffers[0] = vbuf;
    sgbind.index_buffer = ibuf;
    sgbind.fs_images[0] = sg_make_image(&img_desc);

    pass_action.colors[0] = { .action=SG_ACTION_CLEAR, .value={0.5f, 0.0f, 0.0f, 1.0f} };
}

void frame() {
    // step the GBA state forward
    core->runFrame(core);

    sg_image_data image_data{};
    image_data.subimage[0][0] = { .ptr=outputBuffer, .size=(width * height * sizeof(uint32_t)) };
    sg_update_image(sgbind.fs_images[0], image_data);

    sg_begin_default_pass(&pass_action, sapp_width(), sapp_height());
    sg_apply_pipeline(pip);
    sg_apply_bindings(&sgbind);
    sg_draw(0, 6, 1);
    sg_end_pass();
    sg_commit();
}

void cleanup() {
    sg_shutdown();
}

void input(const sapp_event* event) {
    int key = mInputMapKey(&inputMap, SOKOL_BINDING_KEY, event->key_code);
    switch (event->type) {
        case SAPP_EVENTTYPE_KEY_DOWN: {
            core->addKeys(core, 1 << key);
            break;
        }
        case SAPP_EVENTTYPE_KEY_UP: {
            core->clearKeys(core, 1 << key);
            break;
        }
        default: break;
    }
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Please pass ROM path as first parameter.\n";
        return EXIT_FAILURE;
    }

    core = GBACoreCreate();

    core->init(core);
    mCoreInitConfig(core, nullptr);

    struct mCoreOptions opts = {};
    opts.useBios = true;
    opts.logLevel = mLOG_WARN | mLOG_ERROR | mLOG_FATAL;
    mCoreConfigLoadDefaults(&core->config, &opts);
    core->init(core);

    core->desiredVideoDimensions(core, &width, &height);
    outputBuffer = (color_t*)malloc(width * height * BYTES_PER_PIXEL);
    core->setVideoBuffer(core, outputBuffer, width);
//    core->setAudioBufferSize(core, SAMPLES);

    mCoreLoadFile(core, argv[1]);

    core->reset(core);

    thread.core = core;

    mInputPlatformInfo MyGBAInputInfo = {};
    MyGBAInputInfo.platformName = "gba";
    MyGBAInputInfo.nKeys = GBA_KEY_MAX;
    MyGBAInputInfo.hat = {};
    MyGBAInputInfo.hat.up = GBA_KEY_UP;
    MyGBAInputInfo.hat.down = GBA_KEY_DOWN;
    MyGBAInputInfo.hat.right = GBA_KEY_RIGHT;
    MyGBAInputInfo.hat.left = GBA_KEY_LEFT;

    mInputMapInit(&inputMap, &MyGBAInputInfo);
    mInputBindKey(&inputMap, SOKOL_BINDING_KEY, SAPP_KEYCODE_X, GBA_KEY_A);
    mInputBindKey(&inputMap, SOKOL_BINDING_KEY, SAPP_KEYCODE_Z, GBA_KEY_B);
    mInputBindKey(&inputMap, SOKOL_BINDING_KEY, SAPP_KEYCODE_A, GBA_KEY_L);
    mInputBindKey(&inputMap, SOKOL_BINDING_KEY, SAPP_KEYCODE_S, GBA_KEY_R);
    mInputBindKey(&inputMap, SOKOL_BINDING_KEY, SAPP_KEYCODE_ENTER, GBA_KEY_START);
    mInputBindKey(&inputMap, SOKOL_BINDING_KEY, SAPP_KEYCODE_BACKSPACE, GBA_KEY_SELECT);
    mInputBindKey(&inputMap, SOKOL_BINDING_KEY, SAPP_KEYCODE_UP, GBA_KEY_UP);
    mInputBindKey(&inputMap, SOKOL_BINDING_KEY, SAPP_KEYCODE_DOWN, GBA_KEY_DOWN);
    mInputBindKey(&inputMap, SOKOL_BINDING_KEY, SAPP_KEYCODE_LEFT, GBA_KEY_LEFT);
    mInputBindKey(&inputMap, SOKOL_BINDING_KEY, SAPP_KEYCODE_RIGHT, GBA_KEY_RIGHT);

    sapp_desc desc = {};
    desc.init_cb = init;
    desc.frame_cb = frame;
    desc.cleanup_cb = cleanup,
    desc.event_cb = input,
    desc.width = width,
    desc.height = height,
    desc.window_title = "nesemu.cpp",
    desc.icon.sokol_default = true,
    desc.logger.func = slog_func;
    sapp_run(desc);

    mInputMapDeinit(&inputMap);
    mCoreConfigDeinit(&core->config);
    core->deinit(core);
    free(outputBuffer);

    return 0;
}
