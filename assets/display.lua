xMax = 192
yMax = 64
r = 3
balls = {}
num_balls = 0

function add_ball()
    local ball = {}
    ball['x'] = math.random(xMax)
    ball['y'] = math.random(yMax)
    ball['dx'] = math.random() - 0.5 
    ball['dy'] = math.random() - 0.5
    ball['r']=math.random(255)
    ball['g']=math.random(255)
    ball['b']=math.random(255)
    ball['dying']=false
    table.insert(balls,ball)
end

function tableLength(T)
   local count = 0
   for _ in pairs(T) do count = count + 1 end
   return count
end


function update_balls()
    for k,v in pairs(balls) do
        draw_filled_circle(math.floor(v['x']), math.floor(v['y']), r, 0,0,0)
        v['x'] = v['x'] + v['dx']
        v['y'] = v['y'] + v['dy']
        
        if v['y'] >= yMax-r and v['dy'] > 0 then
            v['y'] = yMax-r
            v['dy'] = -v['dy']
        end
        if v['y'] <= r and v['dy'] < 0 then
            v['y'] = r
            v['dy'] = -v['dy']
        end
        if v['x'] >= xMax-r and v['dx'] > 0 then
            v['x'] = xMax-r
            v['dx'] = -v['dx']
        end
        if v['x'] <= r and v['dx'] < 0 then
            v['x'] = r
            v['dx'] = -v['dx']
        end
        draw_filled_circle(math.floor(v['x']), math.floor(v['y']), r, v['r'], v['g'], v['b'])
    end
end

--- Returns a new collection of the items in coll for which pred(item) returns true.
---@param coll table
---@param pred function
---@return table
function Filter(coll, pred)
    local result = {}
    for i=1, #coll do
        if(pred(coll[i])) then
            result[#result+1] = coll[i]
        end
    end
    return result
end


function dead_ball(b) 
    if (b['dead'] == true) then
        return false
    end
    return true
end

ship_color = 0

function ship()
    ship_color = (ship_color + 3) % 255
    x=math.floor(192/2)
    y=64-4
    draw_filled_circle(x,y, 3, ship_color, ship_color, ship_color)
    
    
    for k,v in pairs(balls) do
        dx = v['x'] - x
        dy = v['y'] - y
        
        -- Find balls to kill
        if math.sqrt(dx*dx+dy*dy) < 20 then
            -- Ignore already dying balls
            if v['dying'] == false then
                draw_filled_circle(math.floor(v['x']), math.floor(v['y']), r, 0,0,0)
                v['dying']=true
                v['death_time']=millis() + 500
                v['arc_x'] = math.floor(v['x'])
                v['arc_y'] = math.floor(v['y'])
                v['r']=10
                v['g']=10
                v['b']=10
            end
        end
        
        -- Update Death animation
        if v['dying'] == true then
            draw_line(x, y, v['arc_x'], v['arc_y'], 0,0,0)
            if millis() > v['death_time'] then
                draw_filled_circle(math.floor(v['x']), math.floor(v['y']), r, 0,0,0)
                v['dead'] = true
            else
                v['arc_x'] = math.floor(v['x'])
                v['arc_y'] = math.floor(v['y'])
                draw_line(x, y, v['arc_x'], v['arc_y'], 255,255,0)
            end
        end
    end
    balls = Filter(balls,dead_ball)
end

str="LuaMatrix"

frametimestamp = millis()
frametime=0

function update_frame_time()
    frametime = millis() - frametimestamp
    frametimestamp=millis()
    return frametime
end

tick = millis()
lastframetime=0
while(1) do
    
    update_balls()
    
    draw_string(str,20,20,200,0,0,16)
    ship()
    
    update_frame_time()
    if millis() - tick > 1000 then
        tick = millis()
        draw_string(tostring(lastframetime), 2, 2, 0, 0, 0, 5)
        lastframetime=frametime
        
        if tableLength(balls) < 20 then
            add_ball()
        end
    end
    draw_string(tostring(lastframetime), 2, 2, 0, 255, 0, 5)
end