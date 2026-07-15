# Qt Test 单元测试,针对 viewer-core。

# 共享测试工具:现生成合法 PNG / 现写 zip。
add_library(test-support STATIC support/TestData.cpp)
target_include_directories(test-support PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test-support PUBLIC Qt6::Core Qt6::Gui LibArchive::LibArchive)

# 每个测试一个可执行 + 一条 ctest 用例。
function(add_viewer_test name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE viewer-core test-support Qt6::Test)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

add_viewer_test(test_image_source)
add_viewer_test(test_folder_source)
add_viewer_test(test_archive_source)
add_viewer_test(test_image_decoder)
add_viewer_test(test_image_cache)
add_viewer_test(test_prefetcher)
add_viewer_test(test_browser)
