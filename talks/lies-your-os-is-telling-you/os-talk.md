---
marp: true
theme: gaia
class: invert
paginate: true
---


# otium

[https://otium.sh](https://otium.sh)

<!-- 

hi i'm phil

for the first half of my batch at RC i worked on a little operating system

it's available at this URL if you want to check it out

today i wanted to talk through some of the things i learned about while working on the operating system

-->

---

# lies your operating system is telling you

---

# what even is an operating system

![height:500px](castlevania-meme.png)

<!--
what even is an operating system?
--->

---

# an operating system

- "manages computer hardware and software resources" (Wikipedia)
- "controls all other all other programs that run on a computer" - (reddit /r/explainlikeimfive)
- "handles core tasks like memory management, process scheduling, file management" - google ai

<!--

the google ai answer is actually the most interesting to me

the operating system provides abstractions or lies that allow our programs to be simple even though they're really not
-->

---

# lie #1: code executes sequentially

```c
int main(void) {
  int a = 2;
  int b = 3;
  printf("2 + 3 = %d\n", a + b);
  return 0;
}
```

<!-- 
here we have a nice little sequential program

these statements are definitely going to execute one after the other
-->

---

# here's where it gets complicated

* this code is executed on the processor
* other programs would also like to use the processor
* but the processor has finite resources
* even with multi core processors you still have way less than &lt;running programs worth of resources

---

# so our nice sequential program

```c
int main(void) {
  int a = 2;
  // might get interrupted here
  int b = 3;
  // or here
  printf("2 + 3 = %d\n", a + b);
  // or even here
  return 0;
}
```

<!--

so doesn't matter whether your program is single threaded or not

-->

---

# a scheduler

- the component that handles this is a scheduler
* interleaves execution of different programs
* has to handle potentially bad programs
    * ex what if we do `while(true) continue;`
* has to handle saving registers and other finite resources 
* is pretty hard to implement!

<!--

i bailed out short of implementing a real scheduler, and just made a cooperative
scheduler

in a cooperative scheduler, programs choose when to yield

easy to implement and reason about, but not a great strategy for general purpose operating systems

one badly behaved program can totally wreck your computer

-->

---

# lie #2: memory is infinite

```c
int main(void) {
  void* mem = malloc(4096*4);
  printf("%p\n", mem);
}
```

* when I run this program, I get something like `0x14d808800`
* is this an offset into ram? 
    * does reading to or writing to it do something physical?

---

# virtual memory 

- `0x14d808800` does not guarantee a physical spot in ram
* reading or writing it goes through a page table
    * memory is divided into chunks (pages)
    * only the OS can access memory with its physical address
* it might not even be backed by ram!
    * memory might be copied to the hard drive and back (swap)

---

# why?

![height:500px](ram-meme.jpeg)

<!-- 
there are many reasons for virtual memory, but one is that ram is expensive


--->

---

# lie #3: storage devices are key/value stores

```python
with open("./asdf.txt", "r") as f:
    stuff = f.read()
with open('./asdf.txt', "w") as f:
    f.write("hello world!")
```

* does this code translate to a simple, physical operations that happen in sequence?

* (does that sound familiar?)

---

# filesystems

* storage: actually pretty complicated!
* things to worry about:
    * physical operations are expensive, need to be minimized
    * application use of files needs to be tracked
    * different hardware behaves differently
        * e.g. flash storage tries to distribute use evenly

---

# so our simple file example might...

```python
# check if location of the file is cached in memory
with open("./asdf.txt", "r") as f:
    # check if contents are cached in memory
    # if not, walk through filesystem structures to find physical location
    # actually read the storage
    stuff = f.read()
# same as above
with open('./asdf.txt', "w") as f:
    # buffer small writes to the storage in case more happens
    f.write("hello world!")
```

<!-- 
* check to see if the location of the file is cached in memory
* check to see if contents are cached in memory
* walk through filesystem structures to find the actual physical location
* actually read the storage
* buffer writes to the storage in case more happen
-->

<!--
---

# why not combine them all

* you can `mmap` files into virtual memory, and treat them like they're memory
* you can share memory between processes with `shmem`
* you can enjoy scheduling problems in your own code by writing multithreaded code
-->

<!--

if all these subsystems weren't confusing enough, they can interact with eachother in interesting ways 

--->

---


# thank you operating systems

* operating systems are tricky
* abstractions: actually pretty great
    * i apologize for calling them lies

<!-- 
if that feels like a lot, it's because it is

my main takeaway from this is that i as a non operating systems developer don't have to worry about this stuff too much, but that's because someone else did a lot

lots of ongoing work/research goes into making them reliable

i can't wait to get a job again and allocate memory with reckless abandon 
-->

---

# thanks

## Operating Systems: Three Easy Pieces

[https://ostep.org](https://ostep.org)

<!-- 
if you found any of these things interesting, ostep is a good book

each of the sections of this book correspond to a section of the book

we're also meeting tomorrow to discuss the last chapters of it, so come hang out
-->
