package router

import (
	"testing"
)

func TestRouterClassificationAndCaching(t *testing.T) {
	r, err := NewRouter(100, 300)
	if err != nil {
		t.Fatalf("Failed to initialize router: %v", err)
	}
	defer r.Close()

	// 1. Build classification & cache miss
	prompt1 := "Create me a shopping website with Next.js and FastAPI"
	res1, err := r.Route(prompt1)
	if err != nil {
		t.Fatalf("Route failed: %v", err)
	}
	if res1.Category != "Build" {
		t.Errorf("Expected category 'Build', got '%s'", res1.Category)
	}
	if res1.CacheHit {
		t.Errorf("Expected cache hit = false for first prompt")
	}

	// 2. Identical prompt & cache hit
	res2, err := r.Route(prompt1)
	if err != nil {
		t.Fatalf("Route failed: %v", err)
	}
	if res2.Category != "Build" {
		t.Errorf("Expected category 'Build', got '%s'", res2.Category)
	}
	if !res2.CacheHit {
		t.Errorf("Expected cache hit = true for identical prompt")
	}

	// 3. Question / Chat classification
	prompt3 := "What is the difference between embeddings 1 and 2?"
	res3, err := r.Route(prompt3)
	if err != nil {
		t.Fatalf("Route failed: %v", err)
	}
	if res3.Category != "Question/Chat" {
		t.Errorf("Expected category 'Question/Chat', got '%s'", res3.Category)
	}

	// 4. Small Task classification
	prompt4 := "fix typo in line 12 and update comment"
	res4, err := r.Route(prompt4)
	if err != nil {
		t.Fatalf("Route failed: %v", err)
	}
	if res4.Category != "Small_Task" {
		t.Errorf("Expected category 'Small_Task', got '%s'", res4.Category)
	}

	// Verify cache size
	size := r.GetCacheSize()
	if size != 3 {
		t.Errorf("Expected cache size = 3, got %d", size)
	}
}

func BenchmarkRouter(b *testing.B) {
	r, err := NewRouter(1000, 3600)
	if err != nil {
		b.Fatalf("Failed to initialize router: %v", err)
	}
	defer r.Close()

	prompt := "Build a complex backend microservice with Go and C++"
	b.ResetTimer()

	for i := 0; i < b.N; i++ {
		_, _ = r.Route(prompt)
	}
}
