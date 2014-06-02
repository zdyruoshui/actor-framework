# - Try to find coap
# Once done this will define
#  
#  COAP_FOUND        - system has coap
#  COAP_INCLUDE_DIRS - the coap include directory
#  COAP_LIBRARIES    - link these to use coap
#

# include dir
find_path(COAP_INCLUDE_DIR 
    NAMES coap.h
    PATHS ENV ../libcoap/ ~/libcoap/ ~/Git/libcoap/ ${COAP_ROOT}
)

#library dir
find_library(COAP_LIBRARY
    NAMES libcoap.a 
    PATHS ENV LD_LIBRARY_PATH ../libcoap/ ${COAP_ROOT}
)

set(COAP_INCLUDE_DIRS ${COAP_INCLUDE_DIR} )
set(COAP_LIBRARIES ${COAP_LIBRARY} )

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(libcoap DEFAULT_MSG COAP_INCLUDE_DIR COAP_LIBRARY)

mark_as_advanced(COAP_INCLUDE_DIR COAP_LIBRARY )

