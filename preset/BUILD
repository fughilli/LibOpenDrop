package(default_visibility = ["//:__subpackages__"])

PRESET_DEPS_LIST = [
    "//preset/alien_rorschach",
    "//preset/cube_boom",
    "//preset/glowsticks_3d",
    "//preset/glowsticks_3d_zoom",
    "//preset/kaleidoscope",
    "//preset/rotary_transporter",
    "//preset/simple_preset",
    "//preset/template_preset",
    "//preset/shape_bounce",
    "//preset/eye_roll",
    "//preset/cube_wreath",
    "//preset/pills",
    "//preset/graph_preset",
    "//preset/space_whale_eye_warp",
]

cc_library(
    name = "preset",
    srcs = ["preset.cc"],
    hdrs = ["preset.h"],
    linkstatic = 1,
    deps = [
        "//application:global_state",
        "//util/graphics:gl_interface",
        "//util/graphics:gl_render_target",
        "@com_google_absl//absl/types:span",
    ],
)

cc_library(
    name = "preset_list",
    hdrs = ["preset_list.h"],
    linkstatic = 1,
    deps = [
        ":preset",
        "@com_google_absl//absl/status:statusor",
        "//util/status:status_macros",
    ] + PRESET_DEPS_LIST,
)

cc_library(
    name = "preset_blender",
    srcs = ["preset_blender.cc"],
    hdrs = ["preset_blender.h"],
    deps = [
        ":preset",
        "//primitive:rectangle",
        "//shader:blit_fsh",
        "//shader:blit_vsh",
        "//util/graphics:gl_util",
        "//util/logging",
        "//util/time:oneshot",
    ],
)
