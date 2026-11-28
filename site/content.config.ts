import { defineCollection, defineContentConfig, z } from '@nuxt/content'

export default defineContentConfig({
  collections: {
    docs: defineCollection({
      type: 'page',
      source: 'docs/**',
      schema: z.object({
        title: z.string(),
        description: z.string(),
        badge: z.string().optional(),
        category: z.string().optional(),
        chapter: z.string().optional(),
        part: z.string().optional(),
        baseline: z.string().optional(),
        contracts: z
          .array(
            z.object({
              label: z.string(),
              to: z.string()
            })
          )
          .optional()
      })
    })
  }
})
