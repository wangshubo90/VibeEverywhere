function(sentrits_configure_packaging)
  set(SENTRITS_GENERATED_PACKAGING_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/packaging")
  set(SENTRITS_GENERATED_MACOS_DIR "${SENTRITS_GENERATED_PACKAGING_DIR}/macos")
  set(SENTRITS_GENERATED_SYSTEMD_DIR "${SENTRITS_GENERATED_PACKAGING_DIR}/systemd")
  set(SENTRITS_STAGED_WEB_ROOT "${CMAKE_CURRENT_BINARY_DIR}/packaging/www")
  set(SENTRITS_MACOS_STAGE_ROOT "${CMAKE_CURRENT_BINARY_DIR}/packaging/macos")
  set(SENTRITS_MACOS_INSTALL_ROOT "${SENTRITS_MACOS_STAGE_ROOT}/Sentrits")
  set(SENTRITS_MACOS_ARCHIVE_NAME "sentrits-${PROJECT_VERSION}-macos-${CMAKE_SYSTEM_PROCESSOR}.tar.gz")
  set(SENTRITS_MACOS_ARCHIVE_PATH "${CMAKE_CURRENT_BINARY_DIR}/${SENTRITS_MACOS_ARCHIVE_NAME}")
  set(SENTRITS_WEB_REPO "${CMAKE_CURRENT_SOURCE_DIR}/../Sentrits-Web" CACHE PATH
      "Path to the maintained Sentrits-Web repository checkout")
  set(SENTRITS_PACKAGE_WEB_SOURCE "${SENTRITS_STAGED_WEB_ROOT}" CACHE PATH
      "Web asset directory to install into packaged Sentrits builds")

  file(MAKE_DIRECTORY "${SENTRITS_GENERATED_MACOS_DIR}")
  file(MAKE_DIRECTORY "${SENTRITS_GENERATED_SYSTEMD_DIR}")

  if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(SENTRITS_BIN "/usr/bin/sentrits")
    set(SENTRITS_WEB_ROOT "/usr/${CMAKE_INSTALL_LIBDIR}/sentrits/www")
  else()
    set(SENTRITS_BIN "${CMAKE_INSTALL_FULL_BINDIR}/sentrits")
    set(SENTRITS_WEB_ROOT "${SENTRITS_PACKAGED_WEB_ROOT}")
  endif()

  configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/packaging/macos/io.sentrits.agent.plist.in"
    "${SENTRITS_GENERATED_MACOS_DIR}/io.sentrits.agent.plist"
    @ONLY
  )
  configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/packaging/systemd/sentrits.service.in"
    "${SENTRITS_GENERATED_SYSTEMD_DIR}/sentrits.service"
    @ONLY
  )

  install(TARGETS sentrits
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
  )

  if(APPLE)
    install(FILES
      "${SENTRITS_GENERATED_MACOS_DIR}/io.sentrits.agent.plist"
      DESTINATION "${CMAKE_INSTALL_DATADIR}/sentrits/launchd"
    )
  endif()

  if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    install(FILES
      "${SENTRITS_GENERATED_SYSTEMD_DIR}/sentrits.service"
      DESTINATION "${CMAKE_INSTALL_LIBDIR}/systemd/user"
    )
  endif()

  install(DIRECTORY
    "${SENTRITS_PACKAGE_WEB_SOURCE}/"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/sentrits/www"
  )

  add_custom_target(sentrits_stage_dev_web_assets
    COMMAND "${CMAKE_COMMAND}" -E rm -rf "${SENTRITS_STAGED_WEB_ROOT}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${SENTRITS_STAGED_WEB_ROOT}/host-admin"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${SENTRITS_STAGED_WEB_ROOT}/vendor"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/frontend/dist/host-admin/browser"
            "${SENTRITS_STAGED_WEB_ROOT}/host-admin"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/web/vendor"
            "${SENTRITS_STAGED_WEB_ROOT}/vendor"
    COMMENT "Stage development web assets into ${SENTRITS_STAGED_WEB_ROOT}"
    VERBATIM
  )

  add_custom_target(sentrits_stage_web_assets
    COMMAND "${CMAKE_COMMAND}"
            -DSENTRITS_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
            -DSENTRITS_WEB_REPO="${SENTRITS_WEB_REPO}"
            -DSENTRITS_STAGED_WEB_ROOT="${SENTRITS_STAGED_WEB_ROOT}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/StageWebAssets.cmake"
    COMMENT "Stage packaged web assets from ${SENTRITS_WEB_REPO}"
    VERBATIM
  )

  set(CPACK_PACKAGE_NAME "sentrits")
  set(CPACK_PACKAGE_VENDOR "Sentrits")
  set(CPACK_PACKAGE_CONTACT "shubow")
  set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Sentrits daemon-first runtime packaging")
  set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
  set(CPACK_PACKAGE_FILE_NAME
      "sentrits-${PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")

  if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(CPACK_GENERATOR "DEB")
    set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
    set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
    set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
    set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "https://github.com/shubow-sentrits/Sentrits-Core")
    set(CPACK_DEBIAN_PACKAGE_DESCRIPTION
        "Sentrits daemon-first runtime with packaged static web assets and user-scoped service templates.")
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "systemd")

    add_custom_target(sentrits_package_deb
      DEPENDS sentrits sentrits_stage_web_assets
      COMMAND "${CMAKE_CPACK_COMMAND}" -G DEB --config "${CMAKE_BINARY_DIR}/CPackConfig.cmake"
      COMMENT "Build a Debian package for Sentrits"
      VERBATIM
    )
  endif()

  if(APPLE)
    add_custom_target(sentrits_package_macos
      DEPENDS sentrits sentrits_stage_web_assets
      COMMAND "${CMAKE_COMMAND}" -E rm -rf "${SENTRITS_MACOS_STAGE_ROOT}"
      COMMAND "${CMAKE_COMMAND}" -E make_directory "${SENTRITS_MACOS_STAGE_ROOT}"
      COMMAND "${CMAKE_COMMAND}" --install "${CMAKE_BINARY_DIR}" --prefix "${SENTRITS_MACOS_INSTALL_ROOT}"
      COMMAND "${CMAKE_COMMAND}" -E rm -f "${SENTRITS_MACOS_ARCHIVE_PATH}"
      COMMAND "${CMAKE_COMMAND}" -E chdir "${SENTRITS_MACOS_STAGE_ROOT}"
              "${CMAKE_COMMAND}" -E tar "cfvz" "${SENTRITS_MACOS_ARCHIVE_PATH}" --format=gnutar "Sentrits"
      COMMENT "Build a macOS tarball package for Sentrits"
      VERBATIM
    )
  endif()

  include(CPack)
endfunction()
