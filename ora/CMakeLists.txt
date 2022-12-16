add_library(driver SHARED driver.c bind.c fetch.c define.c util.c)
target_link_libraries(driver ${ORACLE_LIBRARY} -rdynamic)
set_target_properties(driver PROPERTIES PREFIX "" OUTPUT_NAME "driver")

install(TARGETS driver LIBRARY DESTINATION ${TARANTOOL_INSTALL_LIBDIR}/ora)
install(FILES init.lua DESTINATION ${TARANTOOL_INSTALL_LUADIR}/ora)

