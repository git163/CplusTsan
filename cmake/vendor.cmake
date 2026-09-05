# cmake/vendor.cmake — 第三方库离线打包（vendor）支持
# 用法：在顶层 CMakeLists.txt `include(cmake/vendor.cmake)`（先于任何 add_subdirectory），
#      任意子目录再用：
#         cplus_vendor_extract(<tar.gz路径> <包内顶层目录> <输出变量:解压后的源码目录>)
#
# 设计：把 third_party/*.tar.gz 交给 git 管理（仓库里只存压缩包），configure 时自动
# 解压到构建目录 ${PROJECT_BINARY_DIR}/vendor_src/，构建全程离线、固定版本。
# 解压幂等：目标已存在则直接复用，reconfigure 无开销。
# 平台：要求 ${CMAKE_COMMAND} -E tar 可用（Linux/macOS 均满足）。

function(cplus_vendor_extract ARCHIVE ROOT_DIR OUT_SRC_DIR)
  set(dest "${PROJECT_BINARY_DIR}/vendor_src/${ROOT_DIR}")
  if(NOT EXISTS "${dest}/CMakeLists.txt")
    file(MAKE_DIRECTORY "${PROJECT_BINARY_DIR}/vendor_src")
    message(STATUS "Extracting vendor archive: ${ARCHIVE}")
    execute_process(
      COMMAND ${CMAKE_COMMAND} -E tar xzf "${ARCHIVE}"
      WORKING_DIRECTORY "${PROJECT_BINARY_DIR}/vendor_src"
      RESULT_VARIABLE _cplus_vendor_result)
    if(NOT _cplus_vendor_result EQUAL 0)
      message(FATAL_ERROR "failed to extract vendor archive: ${ARCHIVE}")
    endif()
  endif()
  set(${OUT_SRC_DIR} "${dest}" PARENT_SCOPE)
endfunction()