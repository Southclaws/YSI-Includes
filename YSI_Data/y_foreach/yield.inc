#if !defined MAX_NESTED_ITERATORS
	#define MAX_NESTED_ITERATORS (4)
#endif

#define yield%0\32;%1; yield%0%1;
#define yieldreturn%0; Iter_YieldReturn(%0);
#define yieldbreak%0; return;

#define iteryield%9;_:(%9=Iter%9@%0$[_:%2]%9; F@o)&&(I@=-1,Iter_YieldEnter())))?I@:_:F@u:F@l:(F@C:Iter_Func@%0$(),--YSI_gIteratorDepth)?1:2;Iter_YieldLoop();
#define F@C:%0(,%1) %0(%1)

enum E_ITER_YIELD
{
	E_ITER_YIELD_STACK_START, // Where the pointer was before the loop.
	E_ITER_YIELD_HEAP_START,  // Where the pointer was before the loop.
	E_ITER_YIELD_STACK_END,   // Where the pointer was at `yield`.
	E_ITER_YIELD_HEAP_END,    // Where the pointer was at `yield`.
	ResolvedAlloc:E_ITER_YIELD_HEAP_ALLOC,  // `malloc`ed memory.
	ResolvedAlloc:E_ITER_YIELD_STACK_ALLOC, // `malloc`ed memory.
	E_ITER_YIELD_FIRST,       // First call of a pair to `Iter_YieldLoop`.
	E_ITER_YIELD_FRM,         // The iterator function context.
	E_ITER_YIELD_CIP,         // The iterator function continuation.
	E_ITER_YIELD_RETURN,      // The return address from `yield`.
	E_ITER_YIELD_FRAME        // The return frame from `yield`.
}

static stock
	YSI_g_sIteratorStack[MAX_NESTED_ITERATORS][E_ITER_YIELD];

stock
	YSI_gIteratorDepth = -1;

/**
 * <remarks>
 * </remarks>
 */

stock bool:Iter_YieldEnter()
{
	// This is called as:
	//   
	//   iter_var = (I@ = -1, Iter_YieldEnter() ? I@ : (iter_func(), I@));
	//   
	// This means we can skip ever entering the iterator in error cases.  Better
	// yet, we can use the default iterator value for the fail case!  It is
	// important that the `true` case is the fail case, as the return value may
	// often be a non-zero jump address (i.e. the return address).
	if (++YSI_gIteratorDepth >= MAX_NESTED_ITERATORS)
	{
		P:E("Too many nested `foreach` yield loops.  Increase `MAX_NESTED_ITERATORS`.");
		return true;
	}
	{}
	// Save the stack depth at this function's return point (adjust for this
	// function's preamble).
	// TODO:  Make this all inline assembly, but keep it simple for the first
	// test version.
	YSI_g_sIteratorStack[YSI_gIteratorDepth][E_ITER_YIELD_STACK_START] = GetAmxStackBottom() + 12;
	YSI_g_sIteratorStack[YSI_gIteratorDepth][E_ITER_YIELD_HEAP_START]  = GetAmxHeapTop();
	YSI_g_sIteratorStack[YSI_gIteratorDepth][E_ITER_YIELD_STACK_END]   = 0;
	YSI_g_sIteratorStack[YSI_gIteratorDepth][E_ITER_YIELD_HEAP_END]    = 0;
	YSI_g_sIteratorStack[YSI_gIteratorDepth][E_ITER_YIELD_HEAP_ALLOC]  = ResolvedAlloc:0;
	YSI_g_sIteratorStack[YSI_gIteratorDepth][E_ITER_YIELD_STACK_ALLOC] = ResolvedAlloc:0;
	YSI_g_sIteratorStack[YSI_gIteratorDepth][E_ITER_YIELD_FIRST]       = 0;
	YSI_g_sIteratorStack[YSI_gIteratorDepth][E_ITER_YIELD_RETURN]      = GetCurrentFrameReturn();
	YSI_g_sIteratorStack[YSI_gIteratorDepth][E_ITER_YIELD_FRAME]       = GetCurrentFramePreviousFrame();
	return false;
}

/**
 * <remarks>
 * <p>Because of the strange way we manipulate the stack, this function actually
 * gets called twice as often as you would expect.  Essentially, for this
 * (psudo-)loop:</p>
 *   
 *   <code>
 *   for (new i = iter_func(); Iter_YieldLoop(); )                              <br />
 *   {                                                                          <br />
 *   }
 *   </code>
 *   
 * <p>The loop is entered and <c>iter_func()</c> is called.  This indirectly
 * calls <c>yield</c>, which returns to the call point of that function.  The
 * loop check is then entered and <c>Iter_YieldLoop()</c> is called.  Depending
 * on if <c>yield</c> was actually used, the main loop body is entered.  At the
 * end of that iteration, the loop check is run again and so
 * <c>Iter_YieldLoop()</c> is called again.</p>
 * 
 * <p>This is where it gets wierd!</p>
 * 
 * <p><c>Iter_YieldLoop()</c> does a stack copy and a jump in to the earlier
 * call to <c>iter_func</c>, whose return address is earlier in the code.  When
 * a <c>yield</c> is done again, that return is to the first part of the
 * <c>for</c> loop, which then instantly enters the loop check section and calls
 * <c>Iter_YieldLoop()</c> again (as a side-effect, saving the iterator value in
 * the loop variable).</p>
 * 
 * <p>So for <c>N</c> iterations of the loop, <c>Iter_YieldLoop()</c> is called
 * <c>2N + 1</c> times, and should be made aware of which phase of its calls it
 * is in.</p>
 * 
 * <p>This is, of course, made more complicated by nested loops, but that just
 * means we need to store the state on our own stack.</p>
 * </remarks>
 */

stock bool:Iter_YieldLoop()
{
	if ((YSI_g_sIteratorStack[YSI_gIteratorDepth + 1][E_ITER_YIELD_FIRST] ^= 1))
	{
		// If there is nothing allocated here, we fell out of the iterator
		// function and so the loop is over.
		if (!YSI_g_sIteratorStack[YSI_gIteratorDepth + 1][E_ITER_YIELD_STACK_ALLOC])
		{
			// Release our stack.
			return false;
		}
		// Otherwise, the iterator continued, so the loop should as well.
	}
	else
	{
		++YSI_gIteratorDepth;
		// Restore the heap.
		if (YSI_g_sIteratorStack[YSI_gIteratorDepth][E_ITER_YIELD_HEAP_ALLOC])
		{
			SetAmxHeapTop(YSI_g_sIteratorStack[YSI_gIteratorDepth][E_ITER_YIELD_HEAP_END]);
			rawMemcpy(
				YSI_g_sIteratorStack[YSI_gIteratorDepth][E_ITER_YIELD_HEAP_START],
				_:YSI_g_sIteratorStack[YSI_gIteratorDepth][E_ITER_YIELD_HEAP_ALLOC],
				YSI_g_sIteratorStack[YSI_gIteratorDepth][E_ITER_YIELD_HEAP_END] - YSI_g_sIteratorStack[YSI_gIteratorDepth][E_ITER_YIELD_HEAP_START]);
			free(Malloc_Reconcile(YSI_g_sIteratorStack[YSI_gIteratorDepth][E_ITER_YIELD_HEAP_ALLOC]));
			YSI_g_sIteratorStack[YSI_gIteratorDepth][E_ITER_YIELD_HEAP_ALLOC] = ResolvedAlloc:0;
		}
		// Restore the stack.
		SetAmxStackBottom(YSI_g_sIteratorStack[YSI_gIteratorDepth][E_ITER_YIELD_STACK_END]);
		rawMemcpy(
			YSI_g_sIteratorStack[YSI_gIteratorDepth][E_ITER_YIELD_STACK_END],
			_:YSI_g_sIteratorStack[YSI_gIteratorDepth][E_ITER_YIELD_STACK_ALLOC],
			YSI_g_sIteratorStack[YSI_gIteratorDepth][E_ITER_YIELD_STACK_START] - YSI_g_sIteratorStack[YSI_gIteratorDepth][E_ITER_YIELD_STACK_END]);
		free(Malloc_Reconcile(YSI_g_sIteratorStack[YSI_gIteratorDepth][E_ITER_YIELD_STACK_ALLOC]));
		YSI_g_sIteratorStack[YSI_gIteratorDepth][E_ITER_YIELD_STACK_ALLOC] = ResolvedAlloc:0;
		// Jump back in to our earlier function.
		SetAmxFrame(YSI_g_sIteratorStack[YSI_gIteratorDepth][E_ITER_YIELD_FRM]);
		SetAmxNextInstructionPointer(YSI_g_sIteratorStack[YSI_gIteratorDepth][E_ITER_YIELD_CIP]);
		// Technically, we never return from here, but the compiler can't know!
	}
	return true;
}

stock Iter_YieldReturn(value)
{
	// This does the return through the global scope.
	I@ = value;
	static
		tmp_l;
	// Heap.
	tmp_l =
		(YSI_g_sIteratorStack[YSI_gIteratorDepth][E_ITER_YIELD_HEAP_END] = GetAmxHeapTop())
		- YSI_g_sIteratorStack[YSI_gIteratorDepth][E_ITER_YIELD_HEAP_START];
	if (tmp_l)
	{
		YSI_g_sIteratorStack[YSI_gIteratorDepth][E_ITER_YIELD_HEAP_ALLOC] = Malloc_Resolve(malloc(tmp_l / cellbytes));
		rawMemcpy(
			_:YSI_g_sIteratorStack[YSI_gIteratorDepth][E_ITER_YIELD_HEAP_ALLOC],
			YSI_g_sIteratorStack[YSI_gIteratorDepth][E_ITER_YIELD_HEAP_START],
			tmp_l);
	}
	// Stack (excluding this function and intermediate results).
	tmp_l = GetAmxStackBottom();
	tmp_l =
		YSI_g_sIteratorStack[YSI_gIteratorDepth][E_ITER_YIELD_STACK_START] -
		(YSI_g_sIteratorStack[YSI_gIteratorDepth][E_ITER_YIELD_STACK_END] = tmp_l + 16);
	YSI_g_sIteratorStack[YSI_gIteratorDepth][E_ITER_YIELD_STACK_ALLOC] = Malloc_Resolve(malloc(tmp_l / cellbytes));
	rawMemcpy(
		_:YSI_g_sIteratorStack[YSI_gIteratorDepth][E_ITER_YIELD_STACK_ALLOC],
		YSI_g_sIteratorStack[YSI_gIteratorDepth][E_ITER_YIELD_STACK_END],
		tmp_l);
	// Store the data to jump straight back to the caller.
	YSI_g_sIteratorStack[YSI_gIteratorDepth][E_ITER_YIELD_FRM] = GetCurrentFramePreviousFrame();
	YSI_g_sIteratorStack[YSI_gIteratorDepth][E_ITER_YIELD_CIP] = GetCurrentFrameReturn();
	// Go to the caller.  From this point on, we can't use any stack-local
	// storage, because we just destroyed the stack!
	SetAmxFrame(YSI_g_sIteratorStack[YSI_gIteratorDepth][E_ITER_YIELD_FRAME]);
	SetAmxStackBottom(YSI_g_sIteratorStack[YSI_gIteratorDepth][E_ITER_YIELD_STACK_START]);
	SetAmxHeapTop(YSI_g_sIteratorStack[YSI_gIteratorDepth][E_ITER_YIELD_HEAP_START]);
	tmp_l = YSI_g_sIteratorStack[YSI_gIteratorDepth][E_ITER_YIELD_RETURN];
	// No longer in this iterator.
	--YSI_gIteratorDepth;
	SetAmxNextInstructionPointer(tmp_l);
}
