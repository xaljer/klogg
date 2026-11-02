# The Qt product shell. Order matters: the UI links the configuration, crash
# handler and update check targets, so they are declared first.
add_subdirectory(config)
add_subdirectory(crash)
add_subdirectory(updates)
add_subdirectory(ui)
add_subdirectory(app)
