# N00B Allocator

This is the N00B memory allocator, which is a requirement to run N00B-hardened programs. The
allocator works as a drop-in replacement for the libc one. The allocator only works in combination
with the N00B Loader, which reserves the entire memory region that the allocator uses.

## How It Works

N00B requires a certain memory layout, where all objects that are checked need to live in an arena
in a certain size region. Since N00B checks all pointers to objects in the N00B memory region of
virtual address space, pointers to randomly placed objects in that region should never be handled by
N00B-hardened code as this can abort the process! The N00B Loader reserves the entire region that is
checked by N00B, while the allocator ensures all objects are placed correctly in that region. The
N00B allocator will transparently forward calls to the libc allocator when required, which
automaticfally places allocations outside the N00B region as that one has already been reserved by
the N00B Loader.

The result is that non-N00B objects are placed outside of the region checked by N00B, while objects
that should be checked are placed correctly in the region.

## Using the Allocator

The allocator can be used as a drop-in replacement for the standard libc one. The only extra
requirement is that **the process should be loaded using the N00B Loader**, not the default system
loader! See the documentation of the N00B Loader for how to do that.

The allocator can be preloaded for any program using the `LD_PRELOAD` environment variable. It can
also be linked into a program at compile-time. The N00B Loader will automatically preload the
allocator. If the allocator is not installed in the usual location, the environment variable
`LD_LIBRARY_PATH` might need to be set to the directory containing the allocator library.

## Building

The N00B allocator is built automatically as part of N00B. Check the main N00B build documentation.
