# Maintainer tools {#maintainer_tools}

The scripts in `maintainer/` are development tools. They compare a change with
a baseline, check what the library promises, and build the published
documentation. You do not need any of them to build or use LAME, and none of
them is part of a binary package.

There are three kinds.

**Checks that pass or fail.** @ref maintainer_abi checks that a program built
against the previous library still works with this one.
@ref maintainer_check_dist checks that the distribution tarball is complete and
builds after a clean unpack.

**Measurements that compare two builds.** @ref maintainer_build_matrix creates
the build configurations. The speed and quality comparisons use these builds,
so that the compiler and the configure options are the same on both sides.
@ref maintainer_perf measures encoding speed. @ref maintainer_quality measures
perceived quality with PEAQ on a reference corpus. @ref maintainer_coverage
reports which code the tests run.

**Publishing.** @ref maintainer_gen_api_docs builds and installs the two
documentation sets, including this one. @ref maintainer_gen_usage creates the
plain-text `USAGE` from the HTML option reference, so that the two always say
the same thing.

- @subpage maintainer_build_matrix
- @subpage maintainer_perf
- @subpage maintainer_quality
- @subpage maintainer_coverage
- @subpage maintainer_abi
- @subpage maintainer_check_dist
- @subpage maintainer_gen_api_docs
- @subpage maintainer_gen_usage
