# NOOB TODO

## Features to add
- [ ] Re-introduce ASLR to the allocator
- [x] Support `realloc`
- Wrap & tag globals
    - [x] Generate a linker script on the fly
    - [x] Do not wrap "safe" globals (I guess). Not sure if this really matters
    - [ ] Put read-only globals in read-only sections
- Relocate and check stack allocations
    - Create stack allocator
        - [x] First, create one without guard regions
        - [ ] Figure out how to efficiently skip over the guard regions during stack alloc
    - Instrument stack allocations
        - [x] For the first try, just use globals (?) to store the current SP for each monosize-stack
        - [ ] use more aggressive safe stack object finding, with addressibleallocsites
        - [ ] check whether we can insert the SP update routine somewhere else than the function entry (nearestCommonDominator?)
        - [ ] use per-thread stacks
        - [ ] extend support to variable-sized allocas & non-entry block allocas
- [x] Support `calloc`
- [ ] improve the initial freshness search in calloc's get_or_create_arena: SPEC06's `sphinx3` (and I think `gcc`, too) spend like 17-18% of their time there
    * we've since started the freshness search from the back since that's where the most fresh arenas end up. results still unclear
- Implement pointer arithmetic checking
    - [x] wrap arithmetic at dereference sites
    - [x] ensure that only safe pointers escape (call, return, memory, ...)
    - [x] only emit tracked bases for pointers with arithmetic
        * currently, the result of them is unused. perhaps that's enough
- [x] Implement dereference checking
- [x] Optimize the placement of the instrumentation    
- [ ] Support ranged memory access checks 

## Things to fix
- [ ] We do global address tagging via wrapping. Public globals that are visible to an external library and returned from there will have a different address than our tagged address
- [x] Compute the offset mask for arithmetic checking instead of assuming 8-bit tags
