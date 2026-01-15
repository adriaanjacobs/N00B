#include "z_asm.h"
#include "z_syscalls.h"
#include "z_utils.h"
#include "z_elf.h"

#define PAGE_SIZE	4096
#define ALIGN		(PAGE_SIZE - 1)
#define ROUND_PG(x)	(((x) + (ALIGN)) & ~(ALIGN))
#define TRUNC_PG(x)	((x) & ~(ALIGN))
#define PFLAGS(x)	((((x) & PF_R) ? PROT_READ : 0) | \
             (((x) & PF_W) ? PROT_WRITE : 0) | \
             (((x) & PF_X) ? PROT_EXEC : 0))
#define LOAD_ERR	((unsigned long)-1)

extern void z_start(void);

static void z_fini(void)
{
    z_printf("Fini at work\n");
}

static int check_ehdr(Elf_Ehdr *ehdr)
{
    unsigned char *e_ident = ehdr->e_ident;
    return (e_ident[EI_MAG0] != ELFMAG0 || e_ident[EI_MAG1] != ELFMAG1 ||
		e_ident[EI_MAG2] != ELFMAG2 || e_ident[EI_MAG3] != ELFMAG3 ||
	    	e_ident[EI_CLASS] != ELFCLASS ||
		e_ident[EI_VERSION] != EV_CURRENT ||
		(ehdr->e_type != ET_EXEC && ehdr->e_type != ET_DYN)) ? 0 : 1;
}

static unsigned long loadelf_anon(int fd, Elf_Ehdr *ehdr, Elf_Phdr *phdr)
{
	unsigned long minva, maxva;
	Elf_Phdr *iter;
	ssize_t sz;
	int flags, dyn = ehdr->e_type == ET_DYN;
	unsigned char *p, *base, *hint;

	minva = (unsigned long)-1;
	maxva = 0;
	
	for (iter = phdr; iter < &phdr[ehdr->e_phnum]; iter++) {
		if (iter->p_type != PT_LOAD)
			continue;
		if (iter->p_vaddr < minva)
			minva = iter->p_vaddr;
		if (iter->p_vaddr + iter->p_memsz > maxva)
			maxva = iter->p_vaddr + iter->p_memsz;
	}

	minva = TRUNC_PG(minva);
	maxva = ROUND_PG(maxva);

	/* For dynamic ELF let the kernel chose the address. */	
	hint = dyn ? NULL : (void *)minva;
	flags = dyn ? 0 : MAP_FIXED_NOREPLACE;
	flags |= (MAP_PRIVATE | MAP_ANONYMOUS);

	/* Check that we can hold the whole image. */
	base = z_mmap(hint, maxva - minva, PROT_NONE, flags, -1, 0);
	if (base == (void *)-1)
		return -1;
	z_munmap(base, maxva - minva);

	flags = MAP_FIXED_NOREPLACE | MAP_ANONYMOUS | MAP_PRIVATE;
	/* Now map each segment separately in precalculated address. */
	for (iter = phdr; iter < &phdr[ehdr->e_phnum]; iter++) {
		unsigned long off, start;
		if (iter->p_type != PT_LOAD)
			continue;
		off = iter->p_vaddr & ALIGN;
		start = dyn ? (unsigned long)base : 0;
		start += TRUNC_PG(iter->p_vaddr);
		sz = ROUND_PG(iter->p_memsz + off);

		p = z_mmap((void *)start, sz, PROT_WRITE, flags, -1, 0);
		if (p == (void *)-1)
			goto err;
		if (z_lseek(fd, iter->p_offset, SEEK_SET) < 0)
			goto err;
		if (z_read(fd, p + off, iter->p_filesz) !=
				(ssize_t)iter->p_filesz)
			goto err;
		z_mprotect(p, sz, PFLAGS(iter->p_flags));
	}

	return (unsigned long)base;
err:
	z_munmap(base, maxva - minva);
	return LOAD_ERR;
}

static int z_strendswith(const char *str, const char *suffix) {
    size_t len = 0, slen = 0;
    while (str[len]) len++;
    while (suffix[slen]) slen++;
    if (len < slen) return 0;
    for (size_t i = 0; i < slen; i++) {
        if (str[len - slen + i] != suffix[i]) return 0;
    }
    return 1;
}

static int z_strcmp(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(const unsigned char *)s1 - *(const unsigned char *)s2;
}

uintptr_t min_virtual_address(void) {
    char buf[32];
    int fd = z_open("/proc/sys/vm/mmap_min_addr", O_RDONLY);
    uint64_t value = 0;
    ssize_t n;
    int i;

    if (fd < 0) 
		z_errx(1, "Could not open 'mmap_min_addr'");

    n = z_read(fd, buf, sizeof(buf));
    if (n > 0) {
        for (i = 0; i < n; i++) {
            if (buf[i] >= '0' && buf[i] <= '9')
                value = value * 10 + (buf[i] - '0');
            else
                break;
        }
    } else {
		z_errx(1, "Could not read 'mmap_min_addr'");
	}

    if (z_close(fd) < 0) 
        z_errx(1, "Could not close 'mmap_min_addr'");

    return (uintptr_t)value;
}

#define Z_PROG		0
#define Z_INTERP	1

void z_entry(unsigned long *sp, void (*fini)(void))
{
    Elf_Ehdr ehdrs[2], *ehdr = ehdrs;
    Elf_Phdr *phdr, *iter;
    Elf_auxv_t *av;
    char **argv, **env, **p, *elf_interp = NULL;
    unsigned long base[2], entry[2];
    const char *file;
    ssize_t sz;
    int argc, fd, i;
    unsigned long at_phdr = 0, at_phnum = 0, at_entry = 0;
    char *at_execfn = NULL;

    (void)fini;

    /* On entry, SP points to argc. */
    argc = (int)*(sp);
    argv = (char **)(sp + 1);
    env = p = (char **)&argv[argc + 1];
    while (*p++ != NULL)
        ;
    av = (void *)p;

    Elf_auxv_t *aux = av;
    while (aux->a_type != AT_NULL) {
        if (aux->a_type == AT_PHDR) at_phdr = aux->a_un.a_val;
        if (aux->a_type == AT_PHNUM) at_phnum = aux->a_un.a_val;
        if (aux->a_type == AT_ENTRY) at_entry = aux->a_un.a_val;
        if (aux->a_type == AT_EXECFN) at_execfn = (char *)aux->a_un.a_val;
        aux++;
    }

    const char *default_sys_loader = 
#if __x86_64__
    "/usr/lib64/ld-linux-x86-64.so.2";
#elif __aarch64__
    "/usr/lib/ld-linux-aarch64.so.1";
#else
    #error Unrecognized architecture
#endif

    /* 
     * Interpreter Mode Detection:
     * If AT_ENTRY does not match our _start, we were invoked as an ELF interpreter
     * by the kernel. We must find our own path and restart ourselves with explicit arguments.
     */
    if (at_entry && (at_entry != (unsigned long)z_start)) {
        unsigned long target_base = 0;
        Elf_Phdr *target_phdrs = (Elf_Phdr *)at_phdr;
        char *my_path = NULL; // The path to this loader

        // Find binary base address
        for (size_t k = 0; k < at_phnum; k++) {
            if (target_phdrs[k].p_type == PT_PHDR) {
                target_base = at_phdr - target_phdrs[k].p_vaddr;
                break;
            }
        }
        
        // Find PT_INTERP to know who we are (our own path on disk)
        for (size_t k = 0; k < at_phnum; k++) {
            if (target_phdrs[k].p_type == PT_INTERP) {
                my_path = (char *)(target_base + target_phdrs[k].p_vaddr);
                break;
            }
        }

        if (!my_path) 
            z_errx(1, "Running as interpreter but cannot find PT_INTERP in target\n");

        /* Exec: <my_path> <sys_loader> <target_binary> <args...> */
        int new_argc = argc + 2; 
        char **new_argv = z_alloca((new_argc + 1) * sizeof(char *));

        new_argv[0] = my_path;
        new_argv[1] = (char *)default_sys_loader;
        new_argv[2] = at_execfn;
        
        // Copy original arguments (argv[1]..argv[argc-1])
        for (int k = 1; k < argc; k++) {
            new_argv[2 + k] = argv[k];
        }
        new_argv[new_argc] = NULL;

        z_execve(my_path, new_argv, env);
        z_errx(1, "Failed to re-exec self via PT_INTERP path");
    }

    /* 
     * Direct Mode Argument Parsing:
     * Case 1: ./loader <sys_loader> <binary> (The re-exec case)
     * Case 2: ./loader <binary> <args...>   (Manual invocation)
     */
	if (argc < 2)
		z_errx(1, "no input file");

	file = argv[1];
	elf_interp = NULL;

#ifdef NOOB_MAX_ADDR
	uintptr_t vm_min_addr = ROUND_PG(min_virtual_address());
	void* ret = z_mmap((void*) vm_min_addr, NOOB_MAX_ADDR - vm_min_addr, PROT_NONE, MAP_FIXED_NOREPLACE|MAP_NORESERVE|MAP_ANON|MAP_PRIVATE, -1, 0);
	if (ret != (void*) vm_min_addr) 
		z_errx(1, "Can't map N00B region");
#endif

	for (i = 0;; i++, ehdr++) {
		/* Open file, read and than check ELF header.*/
		if ((fd = z_open(file, O_RDONLY)) < 0)
			z_errx(1, "can't open %s", file);
		if (z_read(fd, ehdr, sizeof(*ehdr)) != sizeof(*ehdr))
			z_errx(1, "can't read ELF header %s", file);
		if (!check_ehdr(ehdr))
			z_errx(1, "bogus ELF header %s", file);

		/* Read the program header. */
		sz = ehdr->e_phnum * sizeof(Elf_Phdr);
		phdr = z_alloca(sz);
		if (z_lseek(fd, ehdr->e_phoff, SEEK_SET) < 0)
			z_errx(1, "can't lseek to program header %s", file);
		if (z_read(fd, phdr, sz) != sz)
			z_errx(1, "can't read program header %s", file);
		/* Time to load ELF. */
		if ((base[i] = loadelf_anon(fd, ehdr, phdr)) == LOAD_ERR)
			z_errx(1, "can't load ELF %s", file);

		/* Set the entry point, if the file is dynamic than add bias. */
		entry[i] = ehdr->e_entry + (ehdr->e_type == ET_DYN ? base[i] : 0);
		/* The second round, we've loaded ELF interp. */
		if (file == elf_interp) {
			z_close(fd);
			break;
		}

        if (elf_interp) {
            file = elf_interp;
        } else {
            for (iter = phdr; iter < &phdr[ehdr->e_phnum]; iter++) {
                if (iter->p_type != PT_INTERP)
                    continue;
                elf_interp = z_alloca(iter->p_filesz);
                if (z_lseek(fd, iter->p_offset, SEEK_SET) < 0)
                    z_errx(1, "can't lseek interp segment");
                if (z_read(fd, elf_interp, iter->p_filesz) !=
                        (ssize_t)iter->p_filesz)
                    z_errx(1, "can't read interp segment");
                if (elf_interp[iter->p_filesz - 1] != '\0')
                    z_errx(1, "bogus interp path");
                file = elf_interp;
            }
        }

        z_close(fd);
        /* Looks like the ELF is static -- leave the loop. */
		if (elf_interp == NULL)
			break;
	}

	/* Reassign some vectors that are important for
	 * the dynamic linker and for lib C. */
#define AVSET(t, v, expr) case (t): (v)->a_un.a_val = (expr); break
	while (av->a_type != AT_NULL) {
		switch (av->a_type) {
		AVSET(AT_PHDR, av, base[Z_PROG] + ehdrs[Z_PROG].e_phoff);
		AVSET(AT_PHNUM, av, ehdrs[Z_PROG].e_phnum);
		AVSET(AT_PHENT, av, ehdrs[Z_PROG].e_phentsize);
		AVSET(AT_ENTRY, av, entry[Z_PROG]);
		AVSET(AT_EXECFN, av, (unsigned long)argv[1]);
		AVSET(AT_BASE, av, elf_interp ?
				base[Z_INTERP] : av->a_un.a_val);
		}
		++av;
	}
#undef AVSET
	++av;

	/* Shift argv, env and av. */
	z_memcpy(&argv[0], &argv[1],
		 (unsigned long)av - (unsigned long)&argv[1]);
	/* SP points to argc. */
	(*sp)--;

	z_trampo((void (*)(void))(elf_interp ?
			entry[Z_INTERP] : entry[Z_PROG]), sp, z_fini);
	/* Should not reach. */
	z_exit(0);
}

