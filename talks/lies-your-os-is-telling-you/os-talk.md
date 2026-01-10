---
marp: true
theme: default
class: invert
paginate: true
---

<style>
    .float-left{
    height:500px;
    float:left;
    }
    .float-right {
    float:right;
    height:500px;
    }
</style>


# otium

# [https://otium.sh](https://otium.sh)

<!-- 

hi i'm phil

for the first half of my batch at RC i worked on a little operating system

it's available at this URL if you want to check it out

today i wanted to talk about some of the things i learned about while working on the operating system

-->

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

the operating system provides abstractions which i will refer to as lies that allow programs to be written simply even though they're not

-->

---

# lie #1: code executes sequentially

```python
print("one")
print("two")
print("three")
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
* way more programs than processor cores

---

# so our nice sequential program

```python
# might get interrupted here
print("one")
# or here
print("two")
# or even here
print("three")
```

<!--

so doesn't matter whether your program is single threaded or not

-->

---

# a scheduler

![height:500px](./concurrency-meme.png)

<!--

program that does this is called a scheduler

is pretty hard to implement

is a concurrent program

is the engine of concurrency

needs to handle programs that are misbehaved or go idle

-->

---

# lie #2: memory is infinite

```python
int main(void) {
  void* mem = malloc(1024);
  printf("%p\n", mem);
}
```

* when I run this program, I get something like `0x14d808800`
* is this an offset into ram? 

<!--
when i run this program which just allocates a little bit of memory and prints out its address
-->


---

# virtual memory 

- `0x14d808800` does not guarantee a physical spot in ram
* it might not even be backed by ram!
    * memory might be copied to storage and back (swap)
* so `0x14d808800` gets looked up in a table the operating system has
    * something like `(*mem) = 5;` might actually be pretty complicated

<!--

because of virtual memory subsystem, it is not a physical address
-->

---

# why?

![height:500px](ram-meme.jpeg)

<!-- 
there are many reasons for virtual memory, but one is that ram is expensive

like say you have the new 128gb macbook pro, but you also want to open a web
browser and slack at the same time, so you don't have enough memory. virtual
memory is what lets you do that.

--->

---

# but wait! there's more!

<img src="hard-drive-meme.jpeg" class="float-right" />

* operating system has to handle wildly differing needs
    * with no visibility into what programs actually do!


<!-- 

there's even more stuff that i couldn't fit into 5 minutes

what i find interesting about this is that the operating system has to handle wildly different needs

with no visibility into what programs actually do
-->


---

# thank you operating systems

- operating systems are tricky
* abstractions: actually pretty great
    * i apologize for calling them lies

<!-- 
if that feels like a lot, it's because it is

my main takeaway from this is that i as a non operating systems developer don't have to worry about this stuff too much, but that's because a lot of people did, a lot

abstractions actually pretty great i can't wait to get a job again and allocate memory with reckless abandon 

-->

---

# thank you recurse

## Operating Systems: Three Easy Pieces

[https://ostep.org](https://ostep.org)

## my lil os

[https://otium.sh](https://otium.sh)

<!-- 
if you found any of these things interesting, ostep is a good book

each of the sections of this book correspond to a section of the book

we're also meeting tomorrow to discuss the last chapters of it, so come hang out
-->
