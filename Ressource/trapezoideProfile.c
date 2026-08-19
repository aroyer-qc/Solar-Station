// Trapezoidal profile

int previous_time;
int output_acceleration;
int position_error;

int function(int position_errore)
{
    int output_velocity;
    int current_velocity = get_current_velocity();
    int current_time = get_current_time();
    int direction_multiplier = 1

    if(position_error < 0)
    {
        direction_multiplier = -1
    }

   if(abs(current_velocity) >= MAXIMUM_SPEED)           // if maximum speed has not been reached
   {
       output_velocity = current_velocity + ((direction_multiplier * MAX_ACCELERATION) * (current_time - previous_time));
       output_acceleration = MAX_ACCELERATION;
   }
   else                                                 // if maximum speed has been reached, stay there for now
   {
       output_velocity = MAXIMUM_SPEED;
       outputAcceleration = 0;
   }

   // if we are close enough to the object to begin slowing down
   if(position_error <= (output_velocity * output_velocity) / (2 * MAX_ACCELERATION))
   {
       output_velocity = current_velocity - ((direction_multiplier * MAX_ACCELERATION) * (current_time - previous_time));
       output_acceleration = -MAX_ACCELERATION;
   }
   
   previous_time = current_time
   
   return output_velocity;
}