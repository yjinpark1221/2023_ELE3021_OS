# Default Code Analysis
## kalloc.c
### char* kalloc(void)
kmem.freelist가 physical memory의 빈 페이지를 저장하고 있음
그 중 하나를 할당

### void kfree(char *v)
v가 가리키고 있는 페이지를 freelist의 맨 앞에 매달아줌

## vm.c
### static pte_t* walkpgdir(pde_t *pgdir, const void *va, int alloc)
va에 해당하는 PTE의 주소를 반환한다. 

(pgdir은 page directory의 page number이고
pde는 page directory의 element가 있는 주소이고
*pte & PTE_P (present) 이면 pgtab = *pde의 physical address이다.
present가 아니라면 할당 가능하다면 pgtab에 새로운 물리주소를 할당 후 pgtab에 있는 페이지를 초기화하고, *pde에 새로운 pgtab과 present, writable, user 를 저장해준다.
위 두 경우 모두 pgtab은 va를 포함하는 page table의 주소이다. 

pgtab + va (page table 내에서 va를 담은 page 위치를 가지는 page table element의 주소)를 반환한다. 
)

### PDX 
virtual address -> page directory index 함수이다. shift를 통해 앞에 10bit를  가져온다. 

### PTE_ADDR 
주소의 하위 12bit를 0으로 바꾼 값을 반환한다.

### PTX
virtual address -> page table index 함수이다. shift를 통해 상위 11~20 bit를 가져온다. 

### static int mappages(pde_t *pgdir, void *va, uint size, uint pa, int perm)
va ~ va + size에 해당하는 pte를 만든어 pa ~ pa + size만큼의 주소를 넣는다. 

(내부적으로 walkpgdir를 이용해 pte주소를 가져온다. *pte에 pa를 넣는다.)

### pde_t* setupkvm(void)
kmap을 이용해서 page table의 kernel 부분을 채워넣는다.

### int allocuvm(pde_t *pgdir, uint oldsz, uint newsz)
oldsz -> newsz만큼 프로세스를 키우기 위해 pte를 만들며 pgdir을 채워넣는다. 

### int copyout(pde_t *pgdir, uint va, void *p, uint len)
p ~ p + len만큼 pgdir의 va에 적는다. 

## exec.c
1. path로부터 ip (inode*) 알아내기
2. ip로부터 elf header 읽어오기
3. pgdir 만들기 (커널 부분 채우기)
4. elf 정보를 이용해서 ip를 읽어 pgdir에 적기
5. 1장의 가드 페이지와 1장의 스택 페이지 할당하기 가드페이지는 PTE_U를 지운다.
6. argv에 있는 것들을 ustack에 넣는다. 
