### AGENT_USAGE

In this project, I used **Claude 3.5 Sonnet** as a "Pair Programmer" to assist in the implementation and debugging process. While the agent helped generate code structures, I maintained full control over the logic and performed rigorous code reviews to ensure every implementation adhered to DBMS principles.

#### How I Used the Agent with Full Understanding:

1.  **Logic Guidance & Architectural Review**: 
    For each Lab, I first studied the specifications to understand the core data structures (e.g., LRU-K Replacer, B+ Tree nodes, Volcano Executors). I then directed the agent to generate initial stubs based on my specific requirements and the provided headers.

2.  **Identifying and Solving Critical Kernel Bugs**:
    During development, I identified several deep-level database bugs that the agent initially missed. I guided the agent to fix these by explaining the root causes:
    *   **Lab 2 (B+ Tree)**: I discovered that garbage values in the Buffer Pool were causing `parent_page_id` to be invalid. I instructed the agent to implement explicit initialization in the `Init()` methods.
    *   **Lab 2 (Memory Safety)**: I identified a **pointer invalidation** issue where reassigning a `PageGuard` invalidated previous pointers during tree traversal. I directed the agent to adopt a "Fresh Pointer" pattern (fetching pointers after every fetch/movement).
    *   **Lab 3 (Query Execution)**: I diagnosed why the executors were returning `NULL` values. I realized it was due to lazy tuple loading from disk and commanded the agent to implement a reconstruction logic to populate the `values_` vector.

3.  **Concurrency & Locking Strategy**:
    In Lab 4, I ensured the **Two-Phase Locking (2PL)** protocol was strictly followed. I manually verified the state transitions (Growing to Shrinking) and the lock upgrade logic to ensure no new locks were acquired in the Shrinking phase, which is vital for serializability.

4.  **Template and Compilation Debugging**:
    I used the agent to help explain complex C++17 template errors, but I personally managed the project's build environment within the WSL (Linux) terminal to ensure system-level compatibility.

#### Conclusion:
Every line of code in this repository has been reviewed and tested by me. The AI served as a powerful tool to accelerate the boilerplate implementation, but the core debugging and the resolution of complex logical invariants were driven by my understanding of the OneBase system architecture.