---
title: Boolean Algebra
order: 3
---

# Boolean Algebra

## Where We Left Off

Lesson 2 ended with two expressions for the siren of our security door:

$$
y=s\land\neg(a\land\neg b)\qquad\text{and}\qquad y=s\land(\neg a\lor b).
$$

We claimed they describe the same function, and we know one way to check: build both truth tables and compare them row by row. That works, but it requires $2^n$ rows, and it doesn't tell us how to *find* a good expression in the first place.

In this lesson we answer two questions:

1. **How can we transform an expression without changing the function it describes?** We need a small set of rules.
2. **Given any truth table, how can we systematically obtain a formula for it?** We need an algorithm.

By the end, we will be able to go from a specification to a truth table, from the truth table to a formula, and from that formula to a simpler one, all mechanically.

## The Rules of the Game

**Boolean algebra** is the set of rules that tell us which manipulations of an expression leave the function unchanged. Fortunately, we need very few of them.

Two rules are so familiar that we will use them without comment. Just as with ordinary sums and products, **commutativity** and **associativity** hold for both AND and OR: the order and the grouping of a chain of ANDs (or of ORs) don't matter. That's why we can write $a\land b\land c$ without parentheses and rearrange the terms freely.

The rest of the toolkit fits in a single table. Each rule has two forms, one for AND and one for OR; the second is obtained from the first by exchanging $\land\leftrightarrow\lor$ and $0\leftrightarrow1$. This symmetry is called **duality**. For this lesson we will use $a+b$ for $a\lor b$ and $ab$ for $a\land b$.

| Rule | AND form | OR form |
|---|---|---|
| Distributivity | $a(b+c)=(ab)+(ac)$ | $a+(bc)=(a+b)(a+c)$ |
| Identity | $a\land 1=a$ | $a + 0=a$ |
| Complement | $a\neg a=0$ | $a+\neg a=1$ |
| Idempotency | $aa=a$ | $a+a=a$ |
| De Morgan | $\neg(ab)=\neg a+\neg b$ | $\neg(a+b)=\neg a \neg b$ |

Together with **double negation**, $\neg\neg a=a$, that's all we will need.

A few remarks:

1. Every rule can be verified with a truth table. Since the domains are finite, nothing here has to be taken on faith.
2. The OR form of distributivity has no counterpart in ordinary arithmetic: $2+(3\cdot4)=14$, but $(2+3)\cdot(2+4)=30$. Boolean algebra is genuinely different from the algebra you know.
3. The letters in these rules stand for **any** Boolean expression, not just single variables. We can apply De Morgan to $\neg((a\lor b)\land c)$ by taking $a\lor b$ as one operand and $c$ as the other. The rules also extend to more than two operands: $\neg(a\land b\land c)=\neg a\lor\neg b\lor\neg c$.
4. Other well-known rules (null element, absorption, covering, consensus, and so on) are consequences of these. We will derive them if and when we need them. Even idempotency can be derived from the others, as you'll see in the exercises.

### A Derived Rule: Combining

Let's see how a new rule is derived from the table. Let $p$ be any expression and $a$ a variable. Then

$$
\boxed{pa\lor p\neg a=p}
$$

*Proof.*

$$
\begin{aligned}
pa\lor p\neg a &= p(a\lor\neg a) && \text{(distributivity)}\\
&= p\land 1 && \text{(complement)}\\
&= p && \text{(identity)}
\end{aligned}
$$

This is the **combining rule**, and it will be our workhorse: *if two terms are identical except that one contains a variable and the other contains its complement, that variable disappears.*

For example, with $p=\neg a\,s$ and the variable $b$:

$$
\neg a\,\neg b\,s\ \lor\ \neg a\,b\,s=\neg a\,s.
$$

### Closing the Loop on Lesson 2

We can now prove, without any truth table, that the two expressions for the siren are equivalent:

$$
\begin{aligned}
s\land\neg(a\land\neg b) &= s\land(\neg a\lor\neg\neg b) && \text{(De Morgan)}\\
&= s\land(\neg a\lor b) && \text{(double negation)}
\end{aligned}
$$

## From Truth Table to Formula

Now to the second question. Suppose someone hands us an arbitrary truth table. Is there a recipe that always produces a formula for it?

The idea is simple. Instead of describing the whole function at once, we describe **each row where the output is 1** with a small formula that is true *only* on that row. Then we say: "the output is 1 if we are in the first such row, **or** in the second, **or** in the third, and so on."

### Literals, Products and Minterms

Some terminology:

- A **literal** is a variable or its complement: $a$ or $\neg a$.
- A **product** is the AND of one or more literals, such as $a\neg b$.
- A **minterm** is a product that contains **every** input variable exactly once, either plain or complemented.

Each row of a truth table has its own minterm: for each variable, we write the variable itself if its value in that row is 1, and its complement if its value is 0. For example, in the row $(a,b,s)=(0,1,1)$ the minterm is

$$
\neg a\,b\,s.
$$

A product equals 1 only when all of its literals are 1, and we chose each literal to be 1 exactly when the variable has the value it has in that row. So **a minterm is 1 on its own row and on no other row.**

It's convenient to give each row a number by reading its input values as a binary number: the row $(0,1,1)$ is row 3, and its minterm is called $m_3$.

### The Algorithm

> **Truth table to sum of products**
>
> *Input:* the truth table of a function $f$ with $n$ inputs.
>
> 1. Find all the rows where the output is 1.
> 2. For each of them, write its minterm: AND together one literal per input variable, using the variable if its value in that row is 1 and its complement if it is 0.
> 3. OR together all the minterms.
>
> *If no row has output 1, then $f=0$.*

The result is called the **canonical sum of products** (SOP) of $f$. In compact notation we just list the row numbers, for example $f=\sum m(1,3,7)$.

**Why does it work?** Take any input combination.

- If its row has output 1, its own minterm equals 1, so the OR is 1.
- If its row has output 0, no minterm equals 1 there, because each minterm is 1 only on its *own* row, which is a different one. So the OR is 0.

The formula therefore agrees with the table on every row.

This also settles a claim we made in Lesson 2: the algorithm only uses NOT, AND and OR, and it works for any truth table. So **every Boolean function can be expressed using only NOT, AND and OR.**

### Example: The Security Door

Here is the truth table of the security door from Lesson 2, with the rows numbered:

| Row | $a$ | $b$ | $s$ | $y$ |
|---|---|---|---|---|
| 0 | 0 | 0 | 0 | 0 |
| 1 | 0 | 0 | 1 | **1** |
| 2 | 0 | 1 | 0 | 0 |
| 3 | 0 | 1 | 1 | **1** |
| 4 | 1 | 0 | 0 | 0 |
| 5 | 1 | 0 | 1 | 0 |
| 6 | 1 | 1 | 0 | 0 |
| 7 | 1 | 1 | 1 | **1** |

The output is 1 in rows 1, 3 and 7, so

$$
m_1=\neg a\,\neg b\,s,\qquad m_3=\neg a\,b\,s,\qquad m_7=a\,b\,s,
$$

and

$$
y=\neg a\,\neg b\,s\ \lor\ \neg a\,b\,s\ \lor\ a\,b\,s=\sum m(1,3,7).
$$

We obtained a correct formula without any insight into the problem, just by following the recipe. But it is clearly longer than the expression we found by reasoning in Lesson 2. Let's see if the rules can shrink it.

## Simplifying

The canonical form is always correct, but rarely short. A natural measure of how "big" a formula is is its number of literals, and we'd like to reduce it.

The combining rule suggests a strategy for sums of products: **look for two terms that differ in exactly one literal, and merge them.**

In our formula, $m_1$ and $m_3$ differ only in $b$, and $m_3$ and $m_7$ differ only in $a$. But $m_3$ appears in both pairs. Idempotency comes to the rescue: since $m_3=m_3\lor m_3$, we can **duplicate** a term and use each copy in a different merge.

$$
\begin{aligned}
y &= \neg a\,\neg b\,s\ \lor\ \neg a\,b\,s\ \lor\ a\,b\,s\\
&= \neg a\,\neg b\,s\ \lor\ \neg a\,b\,s\ \lor\ \neg a\,b\,s\ \lor\ a\,b\,s && \text{(idempotency: repeat } m_3\text{)}\\
&= (\neg a\,\neg b\,s\lor\neg a\,b\,s)\lor(\neg a\,b\,s\lor a\,b\,s) && \text{(associativity)}\\
&= \neg a\,s\ \lor\ b\,s && \text{(combining, twice)}\\
&= s(\neg a\lor b) && \text{(distributivity)}
\end{aligned}
$$

We have recovered exactly the expression from Lesson 2. Three different routes (reasoning from the specification, De Morgan, and the mechanical algorithm followed by simplification) lead to the same function.

A word of caution: this procedure is not guaranteed to give the *shortest* formula, and the order in which you merge terms can matter. For small functions a bit of trial and error works well; later we will see a graphical method, Karnaugh maps, that makes the search systematic.

## The Dual Algorithm: Product of Sums

Duality gives us a second algorithm, which works on the rows where the output is **0**.

A **maxterm** is a *sum* of literals that contains every variable exactly once and is 0 on exactly one row. For each variable, we write it plain if its value in that row is **0**, and complemented if its value is **1**.

> **Truth table to product of sums**
>
> 1. Find all the rows where the output is 0.
> 2. For each of them, write its maxterm: OR together one literal per variable, using the variable if its value in that row is 0 and its complement if it is 1.
> 3. AND together all the maxterms.

The result is the **canonical product of sums** (POS).

Which recipe is better? Whichever has fewer rows to deal with. Consider a function of three variables that is 0 *only* in row 5, where $(a,b,c)=(1,0,1)$. The SOP algorithm would need seven minterms, but the POS algorithm gives a single factor:

$$
f=\neg a\lor b\lor\neg c.
$$

Indeed, this sum is 0 only when $a=1$, $b=0$ and $c=1$, and 1 everywhere else.

## Why Does This Matter?

We now have a complete pipeline, and every step is mechanical:

1. **Specification** in ordinary language
2. **Truth table**, by identifying inputs and outputs
3. **Canonical formula**, with the SOP or POS algorithm
4. **Simplified formula**, with the rules of Boolean algebra

The first step still requires human understanding, but the rest could be carried out by a program. That is exactly what design tools do.

And since all the formulas we write are just functions, none of this depends on how the system will eventually be built. That separation between *what* a system does and *how* it is implemented is one of the central ideas of digital design.

### Try It Yourself

1. **Derive a rule.** Show that idempotency, $a\lor a=a$, follows from the other rules. *Hint:* start from $a=a\lor 0$ and replace $0$ using the complement rule.

2. **Apply the algorithm.** Write the canonical SOP of the following function, then simplify it with the combining and idempotency rules. You should end up with a formula of three terms with two literals each.

    | $a$ | $b$ | $c$ | $f$ |
    |---|---|---|---|
    | 0 | 0 | 0 | 1 |
    | 0 | 0 | 1 | 0 |
    | 0 | 1 | 0 | 1 |
    | 0 | 1 | 1 | 0 |
    | 1 | 0 | 0 | 1 |
    | 1 | 0 | 1 | 1 |
    | 1 | 1 | 0 | 0 |
    | 1 | 1 | 1 | 1 |

3. **Use the dual.** A function of three variables $(a,b,c)$ is 0 only in row 6, where $a=1$, $b=1$ and $c=0$. Write it with the POS algorithm, then use De Morgan to show that it equals $\neg(a\,b\,\neg c)$.

4. **Change the code.** Suppose the correct access code of the security door is **01** instead of 10. Build the new truth table, apply the SOP algorithm and simplify the result.
