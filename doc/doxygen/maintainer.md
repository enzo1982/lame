# Maintainer tools {#maintainer_tools}

The harnesses under `maintainer/` are development tooling: they measure a
change against a baseline, check what the library promises, and produce the
published documentation. None of them is needed to build or use LAME, and none
ships in a binary package.

They divide into three kinds.

**Checks that answer a yes/no question about a change.** @ref maintainer_abi
asks whether a program built against the previous library still works against
this one. @ref maintainer_check_dist asks whether the distribution tarball is
complete and builds from a clean unpack.

**Measurements that compare two builds.** @ref maintainer_build_matrix
generates the configurations the other two consume, so that a comparison keeps
the compiler and the configure options out of the difference.
@ref maintainer_perf measures encoding speed; @ref maintainer_quality scores
perceived quality with PEAQ against a reference corpus. @ref maintainer_coverage
reports which code the test suite actually reaches.

**Publishing.** @ref maintainer_gen_api_docs builds and installs the two
documentation sets, this one among them.

- @subpage maintainer_build_matrix
- @subpage maintainer_perf
- @subpage maintainer_quality
- @subpage maintainer_coverage
- @subpage maintainer_abi
- @subpage maintainer_check_dist
- @subpage maintainer_gen_api_docs
