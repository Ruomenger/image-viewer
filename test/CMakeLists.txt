# Qt Test 单元测试,针对 viewer-core。

# 共享测试工具:现生成合法 PNG / 现写 zip / 可控的假来源。
add_library(test-support STATIC support/TestData.cpp support/FakeSource.cpp)
target_include_directories(test-support PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test-support PUBLIC viewer-core Qt6::Core Qt6::Gui LibArchive::LibArchive)

# 每个测试一个可执行 + 一条 ctest 用例(viewer-core 经 test-support 传递链接)。
function(add_viewer_test name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE test-support Qt6::Test)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

add_viewer_test(test_image_source)
add_viewer_test(test_folder_source)
add_viewer_test(test_archive_source)
add_viewer_test(test_image_decoder)
add_viewer_test(test_image_cache)
add_viewer_test(test_decode_scheduler)
add_viewer_test(test_browser)

# 视图层(viewer-ui)测试:离屏平台运行,CI 无显示器也能跑。
function(add_ui_test name)
    add_viewer_test(${name})
    target_link_libraries(${name} PRIVATE viewer-ui)
    set_tests_properties(${name} PROPERTIES ENVIRONMENT QT_QPA_PLATFORM=offscreen)
endfunction()

add_ui_test(test_image_view)
add_ui_test(test_main_window)
