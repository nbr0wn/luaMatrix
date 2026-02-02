xMax = 192
yMax = 64
r = 2
balls = {}
particles = {}
max_balls = 5
max_particles = 10
velocity_adj = 5.0


function add_ball()
    local ball = {}
    ball['x'] = 0
    ball['y'] = 0
    ball['dx'] = 0
    ball['dy'] = 0
    ball['r']=0
    ball['g']=0
    ball['b']=0
    ball['dying']=false
    ball['dead']=true
    table.insert(balls,ball)
end

function add_particle()
    local particle = {}
    particle['x'] = 0
    particle['y'] = 0
    particle['dx'] = 0
    particle['dy'] = 0
    particle['r']=0
    particle['g']=0
    particle['b']=0
    particle['dead']=true
    particle['death_time']=0
    table.insert(particles,particle)
end

-- Pre-allocate objects
for i=1,max_particles do
    add_particle(x,y)
end

for i=1,max_balls do
    add_ball(x,y)
end

function deploy_ball()
    for k,v in pairs(balls) do
        if v['dead'] == true then
            v['x'] = math.random(xMax)
            v['y'] = math.random(yMax)
            v['dx'] = (math.random() - 0.5) / velocity_adj
            v['dy'] = (math.random() - 0.5) / velocity_adj
            v['r']=math.random(255)
            v['g']=math.random(255)
            v['b']=math.random(255)
            v['dying']=false
            v['dead']=false
            return
        end
    end
end

function deploy_particle(x,y)
    for k,v in pairs(particles) do
        if v['dead'] == true then
            v['x'] = x
            v['y'] = y
            v['dx'] = (math.random() - 0.5 ) / (velocity_adj / 2.0)
            v['dy'] = (math.random() - 0.5) / velocity_adj
            v['r']=255
            v['g']=255
            v['b']=0
            v['dead']=false
            v['death_time']=millis() + 2200
            return
        end
    end
end


-- Utility Functions
function tableLength(T)
   local count = 0
   for _ in pairs(T) do count = count + 1 end
   return count
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

function update_balls(delta)
    local ball_count = 0
    for k,v in pairs(balls) do
        if v['dead'] == false then
            ball_count = ball_count + 1
            draw_filled_circle(math.floor(v['x']), math.floor(v['y']), r, 0,0,0)
            v['x'] = v['x'] + v['dx'] * delta
            v['y'] = v['y'] + v['dy'] * delta
            
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
    if ball_count < max_balls then
        deploy_ball()
    end
end

function update_particles(delta)
    for k,v in pairs(particles) do
        if v['dead'] == false then
            set_pixel(math.floor(v['x']), math.floor(v['y']), 0,0,0)
            v['x'] = v['x'] + v['dx'] * delta
            v['y'] = v['y'] + v['dy'] * delta
            
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
            
            -- Add some gravity
            v['dy'] = v['dy'] + 0.02
            
            
            if millis() > v['death_time'] then
                v['dead'] = true
            else
                set_pixel(math.floor(v['x']), math.floor(v['y']), v['r'], v['g'], v['b'])
            end
        end
    end
end

function boom(x,y)
    for i=1,10 do
        deploy_particle(x,y)
    end
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
        
        if v['dead'] == false then 
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
                    boom(math.floor(v['x']), math.floor(v['y']))
                else
                    v['arc_x'] = math.floor(v['x'])
                    v['arc_y'] = math.floor(v['y'])
                    draw_line(x, y, v['arc_x'], v['arc_y'], 255,255,0)
                end
            end
        end
    end
end

str="LuaMatrix"

frametimestamp = millis()
frametime=0
fps=0

function update_frame_time()
    frametime = millis() - frametimestamp
    frametimestamp=millis()
    fps = 1000/frametime
end

tick = millis()
lastframetime=0

timestamp=0
while(1) do
    
    delta=millis()-timestamp
    update_balls(delta)
    update_particles(delta)
    timestamp=millis()
    
    draw_string(str,20,20,200,0,0,16)
    ship()
    
    update_frame_time()
    if millis() - tick > 1000 then
        tick = millis()
        draw_string(tostring(lastframetime), 2, 2, 0, 0, 0, 5)
        lastframetime=frametime
        draw_string(tostring(lastframetime), 2, 2, 0, 255, 0, 5)
        
    end
end