macro(c2ba_recursive_source_group curdir groupname)
    file(GLOB children RELATIVE ${curdir} ${curdir}/*)
    foreach(child ${children})
        if(IS_DIRECTORY ${curdir}/${child})
            c2ba_recursive_source_group(${curdir}/${child} ${groupname}/${child})
        else()
            string(REPLACE "/" "\\" groupname2 ${groupname})
            source_group(${groupname2} FILES ${curdir}/${child})
        endif()
    endforeach()
endmacro()