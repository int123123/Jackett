set(srcs
  "app_main.cpp"
)

# Optionally embed model file if present
set(MODEL_BIN "models/yolov3_tiny_320x320_int8.bin")
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/${MODEL_BIN}")
  idf_component_register(SRCS ${srcs}
    INCLUDE_DIRS "."
    EMBED_FILES ${MODEL_BIN}
  )
  target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_17)
else()
  message(WARNING "YOLO model file not found at ${MODEL_BIN}. Build will succeed, but runtime will fail. Place the model under main/models/.")
  idf_component_register(SRCS ${srcs}
    INCLUDE_DIRS "."
  )
  target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_17)
endif()
