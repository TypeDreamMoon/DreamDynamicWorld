# Tiles the showcase tour's shots (Invoke-ShowcaseTour.ps1) into overview sheets: rows = weather, columns = views; the
# winter morning's cold (frost, thaw, rime); the far land, rows = weather, columns = aerial perspective and height fog on
# and off; and the dusk sequence as a strip.
#   python Make-ShowcaseSheet.py <screenshots folder> <output folder>
import os
import sys
from PIL import Image, ImageDraw

shots, out = sys.argv[1], sys.argv[2]
views = ['Wide', 'Toon', 'Inside', 'Door', 'Pond', 'Grove', 'Snow', 'Car', 'CarGlass', 'Walker', 'Basin', 'Ring']
rows = ['Clear', 'Cloudy', 'Rain', 'Storm', 'AfterRain', 'Snow', 'Dust', 'Night', 'SnowDay', 'DustDay', 'StormNight', 'FogDay', 'WindFog', 'TerrainFog', 'Caustics', 'PondDry', 'PondRain', 'PondAfter', 'PondReceded']
W, H = 480, 270


def sheet(name, rows, cols):
    img = Image.new('RGB', (W * len(cols), H * len(rows)), (0, 0, 0))
    for r, row in enumerate(rows):
        for c, col in enumerate(cols):
            path = os.path.join(shots, 'SHOW_%s_%s.png' % (row, col))
            if not os.path.exists(path):
                continue
            im = Image.open(path).convert('RGB').resize((W, H))
            d = ImageDraw.Draw(im)
            d.rectangle((0, 0, 200, 14), fill=(0, 0, 0))
            d.text((3, 1), '%s / %s' % (row, col), fill=(255, 255, 255))
            img.paste(im, (c * W, r * H))
    img.save(os.path.join(out, name))
    print(name, img.size)


sheet('ShowcaseSheet.png', rows, views)
sheet('ShowcaseCold.png', ['Frost', 'Thaw', 'Rime'], ['Wide', 'Toon', 'Ground', 'Pond', 'Grove', 'Walker'])
sheet('ShowcaseDusk.png', ['Dusk'], ['%02d' % i for i in range(7)])
sheet('ShowcaseFar.png', ['Far_Noon', 'Far_Evening', 'Far_Overcast', 'Far_Fog'],
      ['ApOn_FogOn', 'ApOff_FogOn', 'ApOn_FogOff', 'ApOff_FogOff'])
