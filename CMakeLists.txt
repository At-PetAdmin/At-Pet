cmake_minimum_required(VERSION 3.21)
project(AtPet VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets Network LinguistTools)

qt_add_executable(at-pet WIN32 MACOSX_BUNDLE
    app/main.cpp
    app/petwindow.cpp
    app/petwindow.h
    app/httpapi.cpp
    app/httpapi.h
    app/agentmonitor.cpp
    app/agentmonitor.h
    app/permissiondialog.cpp
    app/permissiondialog.h
    app/agentregistry.h
    app/settingsdialog.cpp
    app/settingsdialog.h
    assets/default/pet.qrc
)

target_link_libraries(at-pet PRIVATE Qt6::Widgets Qt6::Network)
qt_add_translations(at-pet
    TS_FILES translations/at-pet_zh_CN.ts
    RESOURCE_PREFIX "/i18n"
)
target_compile_definitions(at-pet PRIVATE AT_PET_VERSION="${PROJECT_VERSION}")

install(TARGETS at-pet BUNDLE DESTINATION . RUNTIME DESTINATION bin)
