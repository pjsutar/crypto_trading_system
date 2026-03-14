include(FetchContent)

FetchContent_Declare(
    doctest
    GIT_REPOSITORY https://github.com/doctest/doctest.git
    GIT_TAG        v2.4.11
)
FetchContent_MakeAvailable(doctest)

add_executable(test_orderbook
    test_orderbook.cpp
    ${CMAKE_SOURCE_DIR}/src/data/OrderBook.cpp
)

target_include_directories(test_orderbook
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/src/common
        ${CMAKE_SOURCE_DIR}/src/data
        ${Boost_INCLUDE_DIRS}
)

target_link_libraries(test_orderbook
    PRIVATE
        doctest::doctest
        Threads::Threads
        Boost::system
        OpenSSL::SSL
        OpenSSL::Crypto
        nlohmann_json::nlohmann_json
)

add_test(NAME OrderBookTests COMMAND test_orderbook)