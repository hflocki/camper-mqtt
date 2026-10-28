import pcbnew, sys, itertools
b = pcbnew.LoadBoard(sys.argv[1])
OX, OY = 100, 100
mm = pcbnew.ToMM
boxes = {}
for fp in b.GetFootprints():
    xs, ys = [], []
    for g in fp.GraphicalItems():
        if g.GetLayer() in (pcbnew.F_CrtYd, pcbnew.B_CrtYd):
            bb = g.GetBoundingBox(); xs += [bb.GetLeft(), bb.GetRight()]; ys += [bb.GetTop(), bb.GetBottom()]
    if not xs: continue
    boxes[fp.GetReference()] = (mm(min(xs)) - OX, mm(min(ys)) - OY, mm(max(xs)) - OX, mm(max(ys)) - OY)
W = H = None
for d in b.GetDrawings():
    if d.GetLayer() == pcbnew.Edge_Cuts:
        bb = b.GetBoardEdgesBoundingBox(); W, H = mm(bb.GetWidth()), mm(bb.GetHeight())
for r, (x0, y0, x1, y1) in sorted(boxes.items()):
    flag = ''
    if x0 < -0.01 or y0 < -0.01 or x1 > W + 0.01 or y1 > H + 0.01: flag = ' OUTSIDE'
    if '-v' in sys.argv or flag: print(f'{r:5s} {x0:6.1f} {y0:6.1f} {x1:6.1f} {y1:6.1f}{flag}')
for (a, A), (c, C) in itertools.combinations(boxes.items(), 2):
    if A[0] < C[2] and C[0] < A[2] and A[1] < C[3] and C[1] < A[3]:
        ov = (min(A[2], C[2]) - max(A[0], C[0])) * (min(A[3], C[3]) - max(A[1], C[1]))
        if ov > 0.05: print('OVERLAP', a, c, round(ov, 2))
