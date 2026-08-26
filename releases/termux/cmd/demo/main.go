package main

import (
	"fmt"
	"os"
	"razor/pkg/router"
)

func main() {
	fmt.Println("==================================================")
	fmt.Println("         RazorAI Router & Context Cache           ")
	fmt.Println("==================================================")

	r, err := router.NewRouter(100, 300)
	if err != nil {
		fmt.Printf("Error creating router: %v\n", err)
		os.Exit(1)
	}
	defer r.Close()

	testPrompts := []string{
		"Create a fullstack shopping website with React and Node.js",
		"What is the difference between embeddings 1 and 2?",
		"Fix typo on line 42 and update comment",
		"Create a fullstack shopping website with React and Node.js", // Repeats prompt 1 -> Expect Cache Hit!
		"Can you explain how context caching works in LLM systems?",
		"Build an autonomous AI agent framework in C++ and Go",
		"Build an autonomous AI agent framework in C++ and Go",       // Repeats prompt 6 -> Expect Cache Hit!
	}

	for i, prompt := range testPrompts {
		res, err := r.Route(prompt)
		if err != nil {
			fmt.Printf("Route Error [%d]: %v\n", i+1, err)
			continue
		}

		hitStatus := "CACHE MISS"
		if res.CacheHit {
			hitStatus = "⚡ CACHE HIT"
		}

		fmt.Printf("\n[%d] Prompt: \"%s\"\n", i+1, prompt)
		fmt.Printf("    ├── Classified Target : %s\n", res.Category)
		fmt.Printf("    ├── Cache Status      : %s\n", hitStatus)
		fmt.Printf("    └── Confidence Score  : %.2f\n", res.Confidence)
	}

	fmt.Printf("\nTotal Active Context Cache Entries: %d\n", r.GetCacheSize())
	fmt.Println("==================================================")
}
