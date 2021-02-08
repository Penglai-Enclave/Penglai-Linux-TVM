#include <asm/page.h>
#include <asm/pgtable.h>
#include <linux/gfp.h>
#include <linux/pt_area.h>

int PGD_PAGE_ORDER=8;
int PMD_PAGE_ORDER=10;
int PTE_PAGE_NUM;

char *pt_area_vaddr;
unsigned long pt_area_pages;
unsigned long pt_free_pages;
EXPORT_SYMBOL(pt_area_vaddr);
EXPORT_SYMBOL(pt_area_pages);
EXPORT_SYMBOL(pt_free_pages);
EXPORT_SYMBOL(PGD_PAGE_ORDER);
EXPORT_SYMBOL(PMD_PAGE_ORDER);
EXPORT_SYMBOL(alloc_pt_pte_page);

//uintptr_t enclave_module_installed = 0;
//EXPORT_SYMBOL(enclave_module_installed);
// extern int enclave_module_installed;
extern unsigned long _totalram_pages;

struct pt_page_list{
  //struct pt_page_list *prev_page;
  struct pt_page_list *next_page;
};

//struct pt_page_list *pt_page_list = NULL;
//struct pt_page_list *pt_free_list = NULL;
struct pt_page_list *pt_pgd_page_list = NULL;
struct pt_page_list *pt_pgd_free_list = NULL;
struct pt_page_list *pt_pmd_page_list = NULL;
struct pt_page_list *pt_pmd_free_list = NULL;
struct pt_page_list *pt_pte_page_list = NULL;
struct pt_page_list *pt_pte_free_list = NULL;
EXPORT_SYMBOL(pt_pte_page_list);
EXPORT_SYMBOL(pt_pte_free_list);

spinlock_t pt_lock;
EXPORT_SYMBOL(pt_lock);
/* This function allocates a contionuous piece of memory for pt area.
 * PT area is used for storing page tables. After enclave is enabled,
 * any PTW that meets a page table page that does not lies in pt area
 * will incur a page fault.
 * This function can only be called after mm_init() is called
 */
void init_pt_area()
{
  //initialize pt_pages
  //page: computing the number of the page table page 
  unsigned long pages = (_totalram_pages % PTRS_PER_PTE) ? (_totalram_pages/PTRS_PER_PTE + 1) : (_totalram_pages/PTRS_PER_PTE);
  unsigned long order = ilog2(pages - 1) + 4;
  // unsigned long order = 14;

  unsigned long i = 0;

  printk("total page order is%ld\n", order);
  //align with the page size
  pt_area_pages = 1 << order;
  PTE_PAGE_NUM = pt_area_pages - (1<<PGD_PAGE_ORDER) - (1<<PMD_PAGE_ORDER);
  //free pt_page in total
  pt_free_pages=pt_area_pages;
  //allocate the the pt_area range 
  pt_area_vaddr = (void*)__get_free_pages(GFP_KERNEL, order);
  if(pt_area_vaddr == NULL)
  {
    printk("ERROR: init_pt_area: alloc pages for pt area failed!\n");
    while(1){}
  }
  //pages: computing the size of te page metadata space
  pages = pt_area_pages * sizeof(struct pt_page_list);
  pages = (pages % PAGE_SIZE) == 0 ? (pages / PAGE_SIZE) : (pages / PAGE_SIZE + 1);
  order = ilog2(pages - 1) + 1;

  pt_pgd_page_list = (struct pt_page_list* )__get_free_pages(GFP_KERNEL, order);
  pt_pmd_page_list = (struct pt_page_list* )__get_free_pages(GFP_KERNEL, order);
  pt_pte_page_list = (struct pt_page_list* )__get_free_pages(GFP_KERNEL, order);
  if((pt_pgd_page_list == NULL) || (pt_pmd_page_list == NULL) || (pt_pte_page_list == NULL))
  {
    printk("ERROR: init_pt_area: alloc pages for pt_pgd_pmd_pte_page_list failed!\n");
    while(1){}
  }
  spin_lock_init(&pt_lock);
  spin_lock(&pt_lock);
  for (i = 0; i < (1<<PGD_PAGE_ORDER);++i)
  {
    pt_pgd_page_list[i].next_page = pt_pgd_free_list;
    pt_pgd_free_list = &pt_pgd_page_list[i];
  }
  i = 0;
  for (i = 0; i < (1<<PMD_PAGE_ORDER);++i)
  {
    pt_pmd_page_list[i].next_page = pt_pmd_free_list;
    pt_pmd_free_list = &pt_pmd_page_list[i];
  }
  i = 0;
  for (i = 0; i < PTE_PAGE_NUM;++i)
  {
    pt_pte_page_list[i].next_page = pt_pte_free_list;
    pt_pte_free_list = &pt_pte_page_list[i];
  }
  printk("Init_pt_area: 0x%lx/0x%lx pt pages available!\n",pt_free_pages,pt_area_pages);

  spin_unlock(&pt_lock);
}

unsigned long pt_pages_num()
{
  return pt_area_pages;
}

unsigned long pt_free_pages_num()
{
  return pt_free_pages;
}

char* alloc_pt_pgd_page()
{
  unsigned long pt_page_num;
  char* free_page;
  spin_lock(&pt_lock);

  if(pt_pgd_free_list == NULL){
    printk("PANIC: there is no free page in PT area for pgd!\n");
    while(1){}
  }

  pt_page_num = (pt_pgd_free_list - pt_pgd_page_list);
  //need free_page offset
  free_page = pt_area_vaddr + pt_page_num * PAGE_SIZE;
  pt_pgd_free_list = pt_pgd_free_list->next_page;
  pt_free_pages -= 1;

  //printk("alloc_pt_page: va 0x%lx, pa 0x%lx, 0x%lx/0x%lx pt pages left!\n", free_page, __pa(free_page), pt_free_pages, pt_area_pages);
  spin_unlock(&pt_lock);
  if(enclave_module_installed)
  {
    SBI_PENGLAI_ECALL_4(SBI_SM_SET_PTE, SBI_PTE_MEMSET, __pa(free_page), 0, PAGE_SIZE);
  }
  else
  {
    memset(free_page, 0, PAGE_SIZE);
  }
  return free_page;
}

char* alloc_pt_pmd_page()
{
  unsigned long pt_page_num;
  char* free_page;
  spin_lock(&pt_lock);
  if(pt_pmd_free_list == NULL){
    printk("PANIC: there is no free page in PT area for pmd!\n");
    while(1){}
  }
  pt_page_num = (pt_pmd_free_list - pt_pmd_page_list);
  //need free_page offset
  free_page = pt_area_vaddr + (pt_page_num + (1<<PGD_PAGE_ORDER))* PAGE_SIZE;
  pt_pmd_free_list = pt_pmd_free_list->next_page;
  pt_free_pages -= 1;
  spin_unlock(&pt_lock);
  if(enclave_module_installed)
  {
    SBI_PENGLAI_ECALL_4(SBI_SM_SET_PTE, SBI_PTE_MEMSET, __pa(free_page), 0, PAGE_SIZE);
  }
  else
  {
    memset(free_page, 0, PAGE_SIZE);
  }
  return free_page;
}

char* alloc_pt_pte_page()
{
  unsigned long pt_page_num;
  char* free_page;
  spin_lock(&pt_lock);

  if(pt_pte_free_list == NULL){
    printk("PANIC: there is no free page in PT area for pte!\n");
    while(1){}
  }
  pt_page_num = (pt_pte_free_list - pt_pte_page_list);
  //need free_page offset
  free_page = pt_area_vaddr + (pt_page_num + (1<<PGD_PAGE_ORDER) + (1<<PMD_PAGE_ORDER))* PAGE_SIZE;
  pt_pte_free_list = pt_pte_free_list->next_page;
  pt_free_pages -= 1;
  spin_unlock(&pt_lock);
  if(enclave_module_installed)
  {
    SBI_PENGLAI_ECALL_4(SBI_SM_SET_PTE, SBI_PTE_MEMSET, __pa(free_page), 0, PAGE_SIZE);
  }
  else
  {
    memset(free_page, 0, PAGE_SIZE);
  }
  // printk("alloc_pt_pte_page: free_page %lx\n",free_page);
  return free_page;
}

int free_pt_pgd_page(unsigned long page)
{
  /* In this function, it is hard to check whether the page belong to pt area,
   * because it is hard to know what is the paddr of char* page if char* page
   * does not lie in linear mapped virtual space.
   * It is OK to leave it unchecked as the hardware PTW will check whether the
   * page table lies in pt area.
   */
  unsigned long pt_page_num;

  if(((unsigned long)page % PAGE_SIZE)!=0){
    printk("ERROR: free_pt_page: page is not PAGE_SIZE aligned!\n");
    return -1; 
  }
  pt_page_num = ((char*)page - pt_area_vaddr) / PAGE_SIZE;
  if(pt_page_num >= pt_area_pages)
  {
    printk("ERROR: free_pt_page: page is not in pt_area!\n");
    return -1;
  }

  spin_lock(&pt_lock);

  pt_pgd_page_list[pt_page_num].next_page = pt_pgd_free_list;
  pt_pgd_free_list = &pt_pgd_page_list[pt_page_num];
  pt_free_pages += 1;

  spin_unlock(&pt_lock);
  return  0;
}

int free_pt_pmd_page(unsigned long page)
{
  unsigned long pt_page_num;

  if(((unsigned long)page % PAGE_SIZE)!=0){
    printk("ERROR: free_pt_page: page is not PAGE_SIZE aligned!\n");
    return -1; 
  }
  pt_page_num = (((char*)page - pt_area_vaddr) / PAGE_SIZE) - (1<<PGD_PAGE_ORDER);
  if(pt_page_num >= pt_area_pages)
  {
    printk("ERROR: free_pt_page: page is not in pt_area!\n");
    return -1;
  }

  spin_lock(&pt_lock);

  pt_pmd_page_list[pt_page_num].next_page = pt_pmd_free_list;
  pt_pmd_free_list = &pt_pmd_page_list[pt_page_num];
  pt_free_pages += 1;

  spin_unlock(&pt_lock);
  return  0;
}

int free_pt_pte_page(unsigned long page)
{
  unsigned long pt_page_num;
  // printk("free_pt_pte_page: free_page %lx\n",page);
  if(((unsigned long)page % PAGE_SIZE)!=0){
    printk("ERROR: free_pt_page: page is not PAGE_SIZE aligned!\n");
    return -1; 
  }
  pt_page_num = (((char*)page - pt_area_vaddr) / PAGE_SIZE) - (1<<PGD_PAGE_ORDER) - (1<<PMD_PAGE_ORDER);
  if(pt_page_num >= pt_area_pages)
  {
    printk("ERROR: free_pt_page: page is not in pt_area!\n");
    return -1;
  }

  spin_lock(&pt_lock);
  pt_pte_page_list[pt_page_num].next_page = pt_pte_free_list;
  pt_pte_free_list = &pt_pte_page_list[pt_page_num];
  pt_free_pages += 1;
  spin_unlock(&pt_lock);
  return  0;
}
