# NOOB TODO

- [ ] Re-introduce ASLR to the allocator
- [x] Support `realloc`
- [ ] Wrap & tag globals
- Relocate and check stack allocations
    - Create stack allocator
        - [x] First, create one without guard regions
        - [ ] Figure out how to efficiently skip over the guard regions during stack alloc
    - Instrument stack allocations
        - [x] For the first try, just use globals (?) to store the current SP for each monosize-stack
        - [ ] use more aggressive safe stack object finding, with addressibleallocsites
        - [ ] check whether we can insert the SP update routine somewhere else than the function entry (nearestCommonDominator?)
        - [ ] use per-thread stacks
- [x] Support `calloc`
- [ ] improve the initial freshness search in calloc's get_or_create_arena: SPEC06's `sphinx3` (and I think `gcc`, too) spend like 17-18% of their time there
    * we've since started the freshness search from the back since that's where the most fresh arenas end up. results still unclear
