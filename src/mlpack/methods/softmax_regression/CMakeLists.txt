# Define the files we need to compile.
# Anything not in this list will not be compiled into mlpack.
set(SOURCES
  softmax_regression.hpp
  softmax_regression.cpp
  softmax_regression_impl.hpp
  softmax_regression_function.hpp
  softmax_regression_function.cpp
)

# Add directory name to sources.
set(DIR_SRCS)
foreach(file ${SOURCES})
  set(DIR_SRCS ${DIR_SRCS} ${CMAKE_CURRENT_SOURCE_DIR}/${file})
endforeach()
# Append sources (with directory name) to list of all mlpack sources (used at
# the parent scope).
set(MLPACK_SRCS ${MLPACK_SRCS} ${DIR_SRCS} PARENT_SCOPE)

add_category(softmax_regression "classification")
add_cli_executable(softmax_regression)
add_python_binding(softmax_regression)
add_julia_binding(softmax_regression)
add_go_binding(softmax_regression)
add_r_binding(softmax_regression)
add_markdown_docs(softmax_regression "cli;python;julia;go;r" "")
