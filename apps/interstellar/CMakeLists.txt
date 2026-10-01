# Interstellar — a video colour tool with an editor attached. See REQUIREMENTS.md.
#
# Gated on cosmo_core because the RACK IS a hosted CosmoService: without Cosmo there is no colour
# authority, and an Interstellar that cannot grade is not a degraded Interstellar — it is the thing
# two previous builds shipped by mistake.
if(TARGET cosmo_core)
  add_subdirectory(model)    # the .isp + versions: no Cosmo, no pixels
  add_subdirectory(render)   # the frame path: no project model
  add_subdirectory(core)     # the service: rack + model + render
  add_subdirectory(host)
  add_subdirectory(cli)
else()
  message(STATUS "interstellar: skipped (needs cosmo_core — the rack hosts a real CosmoService)")
endif()
