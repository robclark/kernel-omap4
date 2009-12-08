SECTIONS
{
  .data :
    {
      . = ALIGN(32);
      _start_features_driver = .;
      *(.features)
      _end_features_driver = .;
      *(.data)
    } 
}
