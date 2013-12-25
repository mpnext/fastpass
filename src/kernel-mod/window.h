/*
 * window.h
 *
 *  Created on: Dec 23, 2013
 *      Author: yonch
 */

#ifndef WINDOW_H_
#define WINDOW_H_

/**
 * The log of the size of outgoing packet window waiting for ACKs or timeout
 *    expiry. Setting this at < 6 is a bit wasteful since a full word has 64
 *    bits, and the algorithm works with word granularity
 */
#define FASTPASS_WND_LOG			8
#define FASTPASS_WND_LEN			((1 << FASTPASS_WND_LOG) - BITS_PER_LONG)
#define FASTPASS_WND_WORDS			(BITS_TO_LONGS(1 << FASTPASS_WND_LOG))

struct fp_window {
	unsigned long	marked[FASTPASS_WND_WORDS];
	unsigned long summary;
	u64				head;
	u32				head_word;
	u32				num_marked;

};

static inline u32 wnd_pos(u64 tslot)
{
	return tslot & ((1 << FASTPASS_WND_LOG) - 1);
}

static inline u32 summary_pos(struct fp_window *wnd, u32 pos) {
	return (wnd->head_word - BIT_WORD(pos)) % FASTPASS_WND_WORDS;
}

static inline bool wnd_empty(struct fp_window *wnd)
{
	return (wnd->num_marked == 0);
}

/**
 * Assumes seqno is in the correct range, returns whether the bin is unacked.
 */
static inline bool wnd_is_marked(struct fp_window *wnd, u64 seqno)
{
	return !!test_bit(wnd_pos(seqno), wnd->marked);
}

static inline void wnd_mark(struct fp_window *wnd, u64 seqno)
{
	u32 seqno_index = wnd_pos(seqno);
	BUG_ON(wnd_is_marked(wnd, seqno));

	__set_bit(seqno_index, wnd->marked);
	__set_bit(summary_pos(wnd, seqno_index), &wnd->summary);
	wnd->num_marked++;
}

/**
 * marks a consecutive stretch of sequence numbers [seqno, seqno+amount)
 */
static inline void wnd_mark_bulk(struct fp_window *wnd, u64 seqno, u32 amount)
{
	u32 start_index;
	u32 cur_word;
	u32 start_offset;
	u32 end_index;
	u32 end_word;
	u32 end_offset;
	unsigned long mask;

	BUG_ON(time_before_eq64(seqno, wnd->head - FASTPASS_WND_LEN));
	BUG_ON(time_after64(seqno + amount - 1, wnd->head));

	start_index = wnd_pos(seqno);
	start_offset = start_index % BITS_PER_LONG;
	end_index = wnd_pos(seqno + amount - 1);
	end_word = BIT_WORD(end_index);
	end_offset = end_index % BITS_PER_LONG;

	cur_word = BIT_WORD(start_index);
	mask = (~0UL << start_offset);

	if (cur_word == end_word)
		goto end_word;

	/* separate start word and end word */
	/* start word: */
	BUG_ON((wnd->marked[cur_word] & mask) != 0);
	wnd->marked[cur_word] |= mask;
	mask = ~0UL;

	/* intermediate words */
next_intermediate:
	cur_word = (cur_word + 1) % FASTPASS_WND_WORDS;
	if (likely(cur_word != end_word)) {
		BUG_ON(wnd->marked[cur_word] != 0);
		wnd->marked[cur_word] = ~0UL;
		goto next_intermediate;
	}

	/* end word */
	/* changes contained in one word */
end_word:
	mask &= (~0UL >> (BITS_PER_LONG - 1 - end_offset));
	BUG_ON((wnd->marked[cur_word] & mask) != 0);
	wnd->marked[cur_word] |= mask;

	/* update summary */
	mask = ~0UL >> (BITS_PER_LONG - 1 - summary_pos(wnd, start_index));
	mask &= ~0UL << summary_pos(wnd, end_index);
	wnd->summary |= mask;

	/* update num_marked */
	wnd->num_marked += amount;

	return;
}

static inline void wnd_clear(struct fp_window *wnd, u64 seqno)
{
	u32 seqno_index = wnd_pos(seqno);

	BUG_ON(!wnd_is_marked(wnd, seqno));

	__clear_bit(seqno_index, wnd->marked);
	if (unlikely(wnd->marked[BIT_WORD(seqno_index)] == 0))
		__clear_bit(summary_pos(wnd, seqno_index), &wnd->summary);
	wnd->num_marked--;
}

/**
 * Returns (@seqno - first_seqno), where first_seqno is the sequence no. of the
 *    first unacked packet *at* or *before* @seqno if such exists within the
 *    window, or -1 if it doesn't.
 */
static inline s32 wnd_at_or_before(struct fp_window *wnd, u64 seqno)
{
	u32 seqno_index;
	u32 seqno_word;
	u32 seqno_offset;
	u32 result_word_offset;
	unsigned long tmp;

	/* sanity check: seqno shouldn't be after window */
	BUG_ON(time_after64(seqno, wnd->head));

	/* if before window, return -1 */
	if (unlikely(time_before_eq64(seqno, wnd->head - FASTPASS_WND_LEN)))
		return -1;

	/* check seqno's word in marked */
	seqno_index = wnd_pos(seqno);
	seqno_word = BIT_WORD(seqno_index);
	seqno_offset = seqno_index % BITS_PER_LONG;
	tmp = wnd->marked[seqno_word] << (BITS_PER_LONG - 1 - seqno_offset);
	if (tmp != 0)
		return BITS_PER_LONG - 1 - __fls(tmp);

	/* didn't find in first word, look at summary of all words strictly after */
	tmp = wnd->summary >> summary_pos(wnd, seqno_index);
	tmp &= ~1UL;
	if (tmp == 0)
		return -1; /* summary indicates no marks there */

	result_word_offset = __ffs(tmp);
	tmp = wnd->marked[(seqno_word - result_word_offset) % FASTPASS_WND_WORDS];
	return BITS_PER_LONG * result_word_offset + seqno_offset - __fls(tmp);
}

/**
 * Returns the sequence no of the earliest unacked packet.
 * Assumes such a packet exists!
 */
static inline u64 wnd_earliest_marked(struct fp_window *wnd)
{
	u32 word_offset;
	u64 result;
	unsigned long tmp;
	word_offset = __fls(wnd->summary);
	tmp = wnd->marked[(wnd->head_word - word_offset) % FASTPASS_WND_WORDS];

	result = (wnd->head & ~(BITS_PER_LONG-1)) - (word_offset * BITS_PER_LONG)
			+ __ffs(tmp);

	BUG_ON(!wnd_is_marked(wnd, result));
	BUG_ON(wnd_at_or_before(wnd, result - 1) != -1);

	return result;
}

static inline void wnd_reset(struct fp_window *wnd, u64 head)
{
	memset(wnd->marked, 0, sizeof(wnd->marked));
	wnd->head = head;
	wnd->head_word = BIT_WORD(wnd_pos(head));
	wnd->summary = 0UL;
	wnd->num_marked = 0;
}

/**
 * Caller must make sure there are at most FASTPASS_WND_LEN - BITS_PER_LONG
 *    marked slots in the new window (or unmark them first)
 */
static inline void wnd_advance(struct fp_window *wnd, u64 amount)
{
	u64 word_shift = BIT_WORD(wnd->head + amount) - BIT_WORD(wnd->head);
	if (word_shift >= FASTPASS_WND_WORDS) {
		BUG_ON(wnd->num_marked != 0);
		memset(wnd->marked, 0, sizeof(wnd->marked));
		wnd->summary = 0UL;
	} else {
		BUG_ON(!wnd_empty(wnd) &&
				time_before_eq64(wnd_earliest_marked(wnd),
						wnd->head + amount - FASTPASS_WND_LEN));
		wnd->summary <<= word_shift;
	}
	wnd->head += amount;
	wnd->head_word = (wnd->head_word + word_shift) % FASTPASS_WND_WORDS;
}

static inline u64 wnd_head(struct fp_window *wnd)
{
	return wnd->head;
}

#endif /* WINDOW_H_ */
